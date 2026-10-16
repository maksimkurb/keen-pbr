# Multi-WAN Load Balancing

This page describes how to spread new connections over two or more internet uplinks (multi-WAN) on a Debian or Ubuntu host with the `iptables` firewall backend.

{{< callout type="warning" >}}
Load balancing is **not available on Keenetic**: the Keenetic build does not support it and rejects a configuration with `"strategy": "balance"`. Use the router's own multipath there. The `nftables` backend (OpenWrt 24+ with fw4, or any host with `nft`) supports balancing as well, see [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}); the step-by-step guide below targets iptables on Debian/Ubuntu, and the {{< term "NAT" >}}, `rp_filter` and {{< term "fwmark" >}} notes apply to every backend.
{{< /callout >}}

## How it works

A `urltest` or `icmptest` outbound with `"strategy": "balance"` picks one of its working links for every **new** connection, in proportion to the link's `weight` (1-100, default 1). The choice is saved in {{< term "conntrack" >}} (as the connection's {{< term "connmark" >}}), so the whole connection uses one WAN. keen-pbr only sets a **fwmark** on packets in the {{< term "mangle" >}} table and adds `ip rule` entries for these fwmarks. It never changes source addresses, so {{< term "NAT" >}} is up to you.

## Choose the iptables backend

With `"firewall_backend": "auto"` (the default) keen-pbr uses nftables when the `nft` binary exists and falls back to iptables otherwise. Debian and Ubuntu install `nftables` by default, so to use iptables you must ask for it explicitly:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Allowed values are `auto`, `iptables` and `nftables`. Choose iptables when the host already runs an iptables-based stack (Docker, ufw, fail2ban), so all firewall rules live in one place. Both `iptables-nft` (the Debian default) and `iptables-legacy` (`update-alternatives --config iptables`) work.

Balancing on iptables uses the `statistic` match (kernel module `xt_statistic`, part of the stock Debian/Ubuntu kernel). keen-pbr checks it once at service start. If the module does not work, a config with `"strategy": "balance"` is rejected with an error that names `xt_statistic`, and the firewall is left unchanged. iptables balancing is also not available with `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables picks a WAN randomly with the configured probability, so over a small number of connections the split is only statistical. The `default_gateway` route rule field works only with nftables, so on iptables use explicit rules such as `dest_addr` (see the example below).
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

Use `icmptest` instead of `urltest` if you prefer {{< term "ICMP" >}} probes; every link then needs a `target` address (see [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})). Links that fail their checks are removed from the split and come back when they work again.

## Required system setup

### 1. NAT on every WAN

{{< term "NAT" >}} is **mandatory on every WAN interface**. Without it, connections sent to the second WAN leave with the LAN source address, never get a reply and hang, while the health probes still succeed (they use the interface address). The same applies to the host's own traffic: its source address is chosen before keen-pbr sends the packet to another WAN.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, only if you balance IPv6 and use NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Make the rules persistent, for example with `iptables-persistent` (`sudo apt install iptables-persistent`, then `sudo netfilter-persistent save`), or add the equivalent `*nat` lines to `/etc/ufw/before.rules` when you use ufw. If NAT on your host is managed with nftables, add `masquerade` for both interfaces to a `postrouting` chain of type `nat` instead.

### 2. Reverse path filter

Replies to connections sent via the second WAN arrive on an interface the kernel would not itself use to reach that address, and a strict {{< term "reverse path filter" >}} (`rp_filter=1`) drops them. systemd defaults to `2` (loose), which works. Check and fix:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

The kernel uses the larger of the `all` and per-interface values. keen-pbr does not change `rp_filter`.

## fwmarks set by other programs

By default `daemon.skip_marked_packets` is `true`: any packet that already carries a non-zero {{< term "fwmark" >}} (Docker, WireGuard `0xca6c`, Tailscale, {{< term "QoS" >}} scripts) is left alone by keen-pbr entirely, including balancing. Set it to `false` to let keen-pbr route such packets too, but then it can override marks that other software relies on:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

keen-pbr saves its WAN choice in the connection's {{< term "connmark" >}} in {{< term "conntrack" >}}, within `fwmark.mask` (default `0x00FF0000`; `fwmark.start` defaults to `0x00010000`, and each routable outbound uses two fwmark values). Any service that writes the same bits overwrites the choice. Move keen-pbr to bits nobody else uses. The mask must consist of whole hex digits `F` in a row (for example `0xFF000000`):

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

Check what is already in use with `ip rule show` and `sudo iptables -t mangle -S | grep -i mark`. If the mask has too few fwmark values for your outbounds, keen-pbr rejects the configuration. The status page (web UI, `keen-pbr status`, `GET /api/health/routing`) warns about missing {{< term "NAT" >}}, strict `rp_filter`, and other software's firewall rules or `ip rule` entries that use bits inside `fwmark.mask` (`fwmark_mask_conflict`). These are only warnings and do not block applying the configuration.

Tailscale is a special case: moving the mask does not help, see [Routing Breaks When Tailscale Is Enabled]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Known limitation: inbound connections on the second WAN

Balancing only handles connections that LAN clients (and optionally the host) open. Incoming connections to the host on the second WAN (SSH, port forwards) are answered via the main default route, so the reply leaves through the wrong uplink. keen-pbr does not handle this yet. As a manual workaround, set a {{< term "connmark" >}} on incoming connections per interface with `CONNMARK`, restore it into the {{< term "fwmark" >}} of replies in `mangle OUTPUT`, and add an `ip rule fwmark ... lookup <table>` for a table whose default route is that WAN. Use fwmark values outside `fwmark.mask`.

## Troubleshooting

| Symptom | Likely cause and check |
|---|---|
| Connections hang when they go to one WAN, health checks are fine | Missing {{< term "NAT" >}} on that WAN: `sudo iptables -t nat -S POSTROUTING` must list a `MASQUERADE` for each WAN. |
| Packets leave but replies never arrive | Strict `rp_filter`: the effective value (`all` vs. per-interface, the larger wins) must be `2` or `0`. |
| All traffic uses one WAN | fwmarks set by other programs (`skip_marked_packets`), a {{< term "fwmark" >}} mask collision (the health page shows a `fwmark_mask_conflict` warning naming the rule), or the other WAN is unhealthy. Inspect `sudo iptables -t mangle -S` and the outbound status in the web UI or API. |
| Config is rejected with a message about `xt_statistic` | `sudo modprobe xt_statistic`, then restart the service. |
| No iptables rules, `nft` rules appear instead | `firewall_backend` is `auto` and `nft` is installed: set it to `iptables`. |

Logs go to syslog (systemd journal on Debian). Use `--log-level verbose` or `debug` for more detail, see [CLI]({{< relref "/docs/cli" >}}).

