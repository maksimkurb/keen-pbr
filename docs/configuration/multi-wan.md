# Multi-WAN Load Balancing

This page is for a Debian or Ubuntu host connected to two internet providers, where new connections should be spread between them (multi-WAN). It describes the `iptables` firewall backend.

{{< callout type="warning" >}}
Load balancing is **not available on Keenetic**: the Keenetic build does not support it and rejects a configuration with `"strategy": "balance"`. Use the router's own multipath there. The `nftables` backend (OpenWrt 24+ with fw4, or any host with `nft`) supports balancing as well, see [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}). The step-by-step guide below targets iptables on Debian/Ubuntu, and the {{< term "NAT" >}}, `rp_filter` and {{< term "fwmark" >}} (a numeric mark on a packet) notes apply to every backend.
{{< /callout >}}

## How it works

A `urltest` or `icmptest` outbound with `"strategy": "balance"` picks one of its working links for every **new** connection, in proportion to the link's `weight` (1-100, default 1). The whole connection uses one link. Links that fail their checks are removed from the split and come back when they work again.

You set up two things on the system yourself: {{< term "NAT" >}} on every provider and `rp_filter` (see below).

## Choose the iptables backend

With `"firewall_backend": "auto"` (the default) keen-pbr uses nftables when the `nft` binary exists and falls back to iptables otherwise. Debian and Ubuntu install `nftables` by default, so to use iptables you must ask for it explicitly:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Allowed values are `auto`, `iptables` and `nftables`. Choose iptables when the host already runs an iptables-based stack (Docker, ufw, fail2ban), so all firewall rules live in one place. Both `iptables-nft` (the Debian default) and `iptables-legacy` (`update-alternatives --config iptables`) work.

Balancing on iptables uses the kernel module `xt_statistic`, which is part of the stock Debian/Ubuntu kernel. keen-pbr checks it once at service start. If the module does not work, a config with `"strategy": "balance"` is rejected with an error that names `xt_statistic`, and the firewall is left unchanged. iptables balancing is also not available with `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables picks a link randomly with the configured probability, so over a small number of connections the split is only approximate. The `default_gateway` route rule field works only with nftables, so on iptables use explicit rules such as `dest_addr` (see the example below).
{{< /callout >}}

## Example

Two uplinks, `eth1` (30% of new connections) and `eth2` (70%), LAN on `eth0`. If connections from the host itself should be balanced too, turn on **General** > **Settings** > **General** > **Process router's own traffic** (shown in **Advanced** settings mode) (`iproute.process_router_traffic`); by default only forwarded LAN traffic is balanced. The LAN interfaces go in **Processed (inbound) interfaces** (`route.inbound_interfaces`).

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

Use `icmptest` instead of `urltest` if you prefer {{< term "ICMP" >}} probes; every link then needs a `target` address (see [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})).

## Required system setup

### 1. NAT on every WAN

{{< term "NAT" >}} is **mandatory on every WAN interface**. Without it, a packet leaves toward the second provider with an address from your local network, the reply never comes back, and the connection hangs. Health checks still pass, because they use the interface's own address. This applies to the host's own traffic too.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, only if you balance IPv6 and use NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Make the rules persistent, for example with `iptables-persistent` (`sudo apt install iptables-persistent`, then `sudo netfilter-persistent save`), or add the equivalent `*nat` lines to `/etc/ufw/before.rules` if you use ufw. If NAT on the host is managed through nftables, add a `masquerade` rule for both interfaces to a `nat` type `postrouting` chain.

### 2. Reverse path filter (rp_filter)

The kernel's strict filter (`rp_filter=1`) drops a reply that arrives on a different interface than the one the kernel would use to send a packet to that address. With two providers, replies from the second one look exactly like that. The systemd default is `2` (loose mode), which works. Check it and fix it if needed:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

The kernel uses the larger of the `all` value and the per-interface value.

## Marks (fwmark) set by other programs

Packet marks are set not only by keen-pbr but also by Docker, WireGuard (`0xca6c`), Tailscale and {{< term "QoS" >}} scripts. By default keen-pbr leaves a packet that already carries a foreign mark alone entirely, including balancing. In the web UI this is the **General** > **Settings** > **General** > **Skip packets that are already marked** switch (`daemon.skip_marked_packets`). Turn it off to let keen-pbr route such packets too, but then it can override marks that other software relies on:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

If another program writes marks into the same bits as keen-pbr, the link choice gets overwritten. Then move keen-pbr's marks to bits nobody uses. The default is `fwmark.mask` `0x00FF0000` and `fwmark.start` `0x00010000`; every routed outbound uses two values. The mask must be made of consecutive whole hex `F` digits (for example `0xFF000000`):

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

To see what is already in use, run `ip rule show` and `sudo iptables -t mangle -S | grep -i mark`. If the mask does not have enough values for all your outbounds, keen-pbr rejects the configuration. The status page (web UI, `keen-pbr status`, `GET /api/health/routing`) warns about missing {{< term "NAT" >}}, strict `rp_filter`, and firewall or `ip rule` rules of other programs that use bits inside `fwmark.mask` (`fwmark_mask_conflict`). These are warnings only and do not block applying the configuration.

Tailscale is a special case: moving the mask does not help, see [Routing breaks when Tailscale is enabled]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Known limitation: inbound connections on the second WAN

Balancing works only for connections opened by devices in your network (and by the host itself, if enabled). Incoming connections to the host on the second WAN (SSH, port forwards) get their reply via the main default route, so it leaves through the wrong uplink. keen-pbr does not handle this yet. As a manual workaround, set a {{< term "connmark" >}} on incoming connections per interface with `CONNMARK`, restore it into the {{< term "fwmark" >}} of replies in `mangle OUTPUT`, and add an `ip rule fwmark ... lookup <table>` for a table whose default route is that WAN. Use fwmark values outside `fwmark.mask`.

## Troubleshooting

| Symptom | Likely cause and check |
|---|---|
| Connections hang when they go to one WAN, health checks are fine | Missing {{< term "NAT" >}} on that WAN: `sudo iptables -t nat -S POSTROUTING` must list a `MASQUERADE` for each WAN. |
| Packets leave but replies never arrive | Strict `rp_filter`: the effective value (`all` vs. per-interface, the larger wins) must be `2` or `0`. |
| All traffic uses one WAN | Marks set by other programs (**Skip packets that are already marked**), a {{< term "fwmark" >}} mask collision (the health page shows a `fwmark_mask_conflict` warning naming the rule), or the other WAN is unhealthy. Inspect `sudo iptables -t mangle -S` and the outbound status in the web UI or API. |
| Config is rejected with a message about `xt_statistic` | `sudo modprobe xt_statistic`, then restart the service. |
| No iptables rules, `nft` rules appear instead | `firewall_backend` is `auto` and `nft` is installed: set it to `iptables`. |

Logs go to syslog (systemd journal on Debian). Use `--log-level verbose` or `debug` for more detail, see [CLI]({{< relref "/docs/cli" >}}).

