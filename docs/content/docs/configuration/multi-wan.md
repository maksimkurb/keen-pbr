---
title: Multi-WAN Load Balancing
weight: 7
---

This page describes how to spread new connections over two or more internet uplinks (multi-WAN) on a Debian or Ubuntu host with the `iptables` firewall backend.

{{< callout type="warning" >}}
Load balancing is **not available on Keenetic**: it is compiled out and config validation rejects `"strategy": "balance"`. Use the router's own multipath there. The `nftables` backend (OpenWrt 24+ with fw4, or any host with `nft`) supports balancing as well, see [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}); the step-by-step guide below targets iptables on Debian/Ubuntu, and the NAT, `rp_filter` and fwmark notes apply to every backend.
{{< /callout >}}

## How it works

A `urltest` or `icmptest` outbound with `"strategy": "balance"` picks one of its usable members for every **new** connection, in proportion to the member `weight` (1-100, default 1). The choice is stored in the conntrack mark, so the rest of the connection stays on the same WAN. keen-pbr only **marks** packets in the mangle table and adds routing rules for the marks. It never changes source addresses, so NAT is up to you.

## Choose the iptables backend

With `"firewall_backend": "auto"` (the default) keen-pbr uses nftables when the `nft` binary exists and falls back to iptables otherwise. Debian and Ubuntu install `nftables` by default, so to use iptables you must ask for it explicitly:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Allowed values are `auto`, `iptables` and `nftables`. Choose iptables when the host already runs an iptables-based stack (Docker, ufw, fail2ban), so all firewall rules live in one place. Both `iptables-nft` (the Debian default) and `iptables-legacy` (`update-alternatives --config iptables`) work.

Balancing on iptables uses the `statistic` match (kernel module `xt_statistic`, part of the stock Debian/Ubuntu kernel). Its availability is probed once when the service starts. If it is unusable, keen-pbr refuses to apply a config with a balance outbound and reports an error naming `xt_statistic`, before any firewall change is made. iptables balancing is also not available with `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables picks a WAN randomly with the configured probability, so over a small number of connections the split is only statistical. The `default_gateway` route rule matcher is nftables-only, so on iptables use explicit rules such as `dest_addr` (see the example below).
{{< /callout >}}

## Example

Two uplinks, `eth1` (30% of new connections) and `eth2` (70%), LAN on `eth0`. Add `"iproute": { "process_router_traffic": true }` if connections that originate on the host itself should be balanced too (by default only forwarded LAN traffic is).

```json { filename="/etc/keen-pbr/config.json" }
{
  "daemon": { "firewall_backend": "iptables" },
  "outbounds": [
    { "type": "interface", "tag": "wan1", "interface": "eth1", "gateway": "192.0.2.1" },
    { "type": "interface", "tag": "wan2", "interface": "eth2", "gateway": "198.51.100.1" },
    { "type": "ignore", "tag": "direct_local" },
    {
      "type": "urltest",
      "tag": "wan_balance",
      "url": "https://www.gstatic.com/generate_204",
      "interval_ms": 10000,
      "strategy": "balance",
      "outbound_groups": [
        {
          "members": [
            { "outbound": "wan1", "weight": 3 },
            { "outbound": "wan2", "weight": 7 }
          ]
        }
      ]
    }
  ],
  "lists": {
    "local_networks": {
      "ip_cidrs": [
        "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
        "169.254.0.0/16", "172.16.0.0/12", "192.168.0.0/16",
        "224.0.0.0/4", "240.0.0.0/4"
      ]
    }
  },
  "route": {
    "inbound_interfaces": ["eth0"],
    "rules": [
      { "list": ["local_networks"], "outbound": "direct_local" },
      { "dest_addr": "0.0.0.0/0", "outbound": "wan_balance" }
    ]
  }
}
```

Use `icmptest` instead of `urltest` if you prefer ICMP probes; every member then needs a literal `target` address (see [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})). Members that fail their health checks are removed from the split and returned when they recover.

## Required system setup

### 1. NAT on every WAN

NAT is **mandatory on every WAN interface**. Without it, connections sent to the second WAN leave with the LAN source address, never get a reply and hang, while the health probes still succeed (they use the interface address). The same applies to host-originated traffic, since the source address is chosen before the packet is re-routed.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, only if you balance IPv6 and use NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Make the rules persistent, for example with `iptables-persistent` (`sudo apt install iptables-persistent`, then `sudo netfilter-persistent save`), or add the equivalent `*nat` lines to `/etc/ufw/before.rules` when you use ufw. If NAT on your host is managed with nftables, add `masquerade` for both interfaces to a `postrouting` chain of type `nat` instead.

### 2. Reverse path filter

Replies of a connection that was sent via the second WAN can arrive on an interface that is not the best route back to the source, so strict reverse path filtering (`rp_filter=1`) drops them. systemd defaults to `2` (loose), which works. Check and fix:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

The kernel uses the larger of the `all` and per-interface values. keen-pbr does not change `rp_filter`.

## Foreign packet marks

By default `daemon.skip_marked_packets` is `true`: any packet that already carries a non-zero fwmark (Docker, WireGuard `0xca6c`, Tailscale, QoS scripts) is left alone by keen-pbr entirely, including balancing. Set it to `false` to let keen-pbr route such packets too, but then it can override marks that other software relies on:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

keen-pbr saves its WAN choice in the conntrack mark under `fwmark.mask` (default `0x00FF0000`; `fwmark.start` defaults to `0x00010000`, and each routable outbound uses two marks). Any service that writes the same connmark bits overwrites the choice. Tailscale marks packets with `0x40000` and `0x80000`, which lie inside the default mask. Move keen-pbr to bits nobody else uses. The mask must be nibble-aligned with consecutive `F` nibbles:

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

Check what is already in use with `ip rule show` and `sudo iptables -t mangle -S | grep -i mark`. Config validation reports an error if the mask has too few marks for your outbounds. The health page (`GET /api/health/routing`, `keen-pbr status` and the web UI overview) warns about missing NAT, strict `rp_filter` and `fwmark_mask_conflict`: foreign iptables `MARK`/`CONNMARK` rules (mangle and raw, iptables backend only) or `ip rule` entries that write or match bits inside `fwmark.mask`. These warnings never block apply.

## Known limitation: inbound connections on the second WAN

Balancing only handles connections that LAN clients (and optionally the host) open. Incoming connections to the host on the second WAN (SSH, port forwards) are answered via the main default route, so the reply leaves through the wrong uplink. keen-pbr does not handle this yet. As a manual workaround, mark incoming connections per interface with `CONNMARK`, restore the mark on replies in `mangle OUTPUT`, and add an `ip rule fwmark ... lookup <table>` for a table whose default route is that WAN, using a mark outside `fwmark.mask`.

## Troubleshooting

| Symptom | Likely cause and check |
|---|---|
| Connections hang when they go to one WAN, health checks are fine | Missing NAT on that WAN: `sudo iptables -t nat -S POSTROUTING` must list a `MASQUERADE` for each WAN. |
| Packets leave but replies never arrive | Strict `rp_filter`: the effective value (`all` vs. per-interface, the larger wins) must be `2` or `0`. |
| All traffic uses one WAN | Foreign marks (`skip_marked_packets`), a fwmark mask collision (the health page shows a `fwmark_mask_conflict` warning naming the rule), or the other WAN is unhealthy. Inspect `sudo iptables -t mangle -S` and the outbound status in the web UI or API. |
| Config is rejected with a message about `xt_statistic` | `sudo modprobe xt_statistic`, then restart the service. |
| No iptables rules, `nft` rules appear instead | `firewall_backend` is `auto` and `nft` is installed: set it to `iptables`. |

Logs go to syslog (systemd journal on Debian). Use `--log-level verbose` or `debug` for more detail, see [CLI]({{< relref "/docs/cli" >}}).
