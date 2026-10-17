---
title: Multi-WAN Load Balancing
weight: 7
---

This page is for a router or host connected to two or more internet providers, where new connections should be spread between them (multi-WAN).

{{< callout type="warning" >}}
Load balancing is **not available on Keenetic**: the Keenetic build does not support it and rejects a configuration with `"strategy": "balance"`. Use the router's own multipath there.
{{< /callout >}}

## How it works

A `urltest` or `icmptest` outbound with `"strategy": "balance"` picks one of its working links for every **new** connection, in proportion to the link's `weight` (1-100, default 1). The whole connection uses one link. Links that fail their checks are removed from the split and come back when they work again.

You need to set up {{< term "NAT" >}} on every provider and `rp_filter` on the system (see below).

{{< callout type="info" >}}
With the `iptables` backend, links are picked at random with the configured probability, so over a small number of connections the shares are only approximate. The `iptables` backend needs the `xt_statistic` kernel module, and the `default_gateway` route rule field works only with `nftables`. You normally do not need to change `firewall_backend`: keen-pbr picks the backend that fits the system.
{{< /callout >}}

## Example

Two uplinks, `eth1` (30% of new connections) and `eth2` (70%), LAN on `eth0`. If connections from the host itself should be balanced too, turn on **General** > **Settings** > **General** > **Process router's own traffic** (shown in **Advanced** settings mode); by default only traffic from LAN devices is balanced. The LAN interfaces go in **Processed (inbound) interfaces**.

```json { filename="/etc/keen-pbr/config.json" }
{
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

{{< term "NAT" >}} is **mandatory on every WAN interface**. Without it, a packet leaves toward the second provider with an address from your local network, the reply never comes back, and the connection hangs. Link checks still pass, so the problem is easy to miss. This applies to the host's own traffic too.

{{< only openwrt >}}\
In **Network** > **Firewall** > **Zones**, enable **Masquerading** on every zone that contains an uplink (by default, the `wan` zone).

{{< only nftables >}}\
Add a `masquerade` rule for both interfaces, for example in `/etc/nftables.conf`:

```nft {filename="/etc/nftables.conf"}
table inet multiwan_nat {
  chain postrouting {
    type nat hook postrouting priority srcnat; policy accept;
    oifname { "eth1", "eth2" } masquerade
  }
}
```

{{< only iptables >}}\
Add `MASQUERADE` rules and make them persistent, for example with `iptables-persistent`:

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

### 2. rp_filter

The strict reverse path check (`rp_filter=1`) drops replies from the second provider. Loose mode (`2`) is needed. Check the values and fix them if needed:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

On OpenWrt, put the same three lines in `/etc/sysctl.conf` and run `/etc/init.d/sysctl restart`.

## If other programs on the router use packet marks

Packet marks are set not only by keen-pbr but also by Docker, WireGuard, Tailscale, {{< term "QoS" >}} scripts and mwan3 on OpenWrt. By default keen-pbr leaves packets with a foreign mark alone, so they are not balanced. This is controlled by the **General** > **Settings** > **General** > **Skip packets that are already marked** switch.

If the link choice gets overwritten by foreign marks, the status page shows an `fwmark_mask_conflict` warning. Then move keen-pbr's marks to bits nobody uses:

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

This does not help with Tailscale, see [Routing breaks when Tailscale is enabled]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Limitation: inbound connections on the second WAN

Balancing works only for connections opened by devices in your network (and by the host itself, if enabled). Incoming connections on the second WAN (SSH, port forwards) get their reply via the main connection, that is, through the other provider, and may not work.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Some connections hang, link checks are fine | {{< term "NAT" >}} is enabled on every WAN. |
| Packets leave but replies never arrive | `rp_filter` is `2` or `0` (the larger of the `all` and per-interface values wins). |
| All traffic uses one WAN | The other WAN is unhealthy (status on the **Routing** > **Outbounds** page) or foreign marks (see above). |
| Config is rejected with a message about `xt_statistic` | `sudo modprobe xt_statistic`, then restart the service. |

Logs go to syslog (`logread` on OpenWrt). Use `--log-level verbose` or `debug` for more detail, see [CLI]({{< relref "/docs/cli" >}}).
