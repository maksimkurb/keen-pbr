---
title: Outbounds
weight: 1
---

An outbound is a channel that traffic can take: a VPN interface, your ISP connection, or a block. You choose which traffic goes to which outbound in [route rules]({{< relref "/docs/configuration/route-rules" >}}).

**In the Web UI:** open **Routing** > **Outbounds**, click **Add outbound**, enter a **Name** (called `tag` in the config) and pick a type. Then click **Apply** in the draft bar.

The name (`tag`) uses lowercase Latin letters, digits and `_`, starts with a letter, and is at most 24 characters long (`^[a-z][a-z0-9_]*$`).

Most users only need:

- one **Interface** (`interface`) outbound for the VPN connection
- one **Routing table** (`table`) outbound with `table: 254` (the system's main routing table) for the normal connection, often `wan`
- optionally one **Auto-select (urltest)** outbound, so keen-pbr picks a working one among several connections

If you are not sure, start with **Interface** and come back to the other types later.

## Common Example

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1",
      "gateway6": "2001:db8::1"
    },
    {
      "type": "table",
      "tag": "wan",
      "table": 254
    }
  ]
}
```

## Types

## `interface`

Use it when traffic must go through a specific network interface: a VPN tunnel (`tun0`, `wg0`) or a separate internet connection.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"interface"` |
| `interface` | string | yes | Egress network interface name (e.g. `tun0`) |
| `gateway` | string | no | IPv4 gateway address or `"auto"`. In the Web UI: **Default gateway IPv4** |
| `gateway6` | string | no | IPv6 gateway address or `"auto"`. In the Web UI: **Default gateway IPv6** |
| `strict_enforcement` | boolean | no | Whether to block this outbound's traffic while its interface or gateway is down. If unset, the global **Block traffic when an outbound is unavailable (kill-switch)** setting in **General** > **Settings** > **General** applies |
| `strict_enforcement_action` | string | no | How to block: `"unreachable"` (connections fail immediately) or `"blackhole"` (packets are silently dropped) |

For tunnels such as `tun0` or `wg0`, a gateway is usually not needed: without `gateway` and `gateway6`, both IPv4 and IPv6 traffic is sent straight into the interface. You need a gateway when the interface is connected to your ISP's router and traffic must be handed to its address, for example a second WAN.

You do not have to look the gateway up yourself: enter `"auto"` (type `auto` in the Web UI) and keen-pbr takes the gateway from this interface's default route in the main routing table (if there are several, from the one with the lowest metric). If the interface has no such route, or its best routes have different gateways, the outbound counts as unavailable for that IP version.

If you set a gateway for only one IP version, traffic of the other version for this outbound is blocked rather than leaking past the VPN.

**Kill-switch** in the outbound form sets what happens to traffic while the interface or gateway is down. Options: **Default behavior** (the global setting applies), **Do not block** (traffic bypasses the outbound via the main route), **Block with an error** (`strict_enforcement_action: "unreachable"`), **Block silently** (`"blackhole"`).

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1",
      "gateway6": "2001:db8::1"
    }
  ]
}
```

## `table`

Use it when traffic must follow an existing routing table (a set of system routes): `254` is the main table, meaning the normal internet connection, or a table another service has created.

To see which tables exist and their names, run:

```bash {filename="bash"}
ip rule show
cat /etc/iproute2/rt_tables
```

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"table"` |
| `table` | integer | yes | Existing routing table number. In the Web UI: **Table ID** |

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "table",
      "tag": "custom_table",
      "table": 200
    }
  ]
}
```

## `blackhole`

Use it to block access to sites or addresses: matching traffic is dropped.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"blackhole"` |

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "blackhole",
      "tag": "block"
    }
  ]
}
```

## `ignore`

Use it for exceptions: traffic follows the router's normal routing, as if keen-pbr did not touch it.

Rules are checked from top to bottom and the first match wins, so put the `ignore` rule above a broader one. For example, the whole `streaming` list goes through the VPN except addresses from the `streaming_direct` list (`direct` is the `ignore` outbound from the example below):

```json { filename="config.json" }
"rules": [
  { "list": ["streaming_direct"], "outbound": "direct" },
  { "list": ["streaming"], "outbound": "vpn" }
]
```

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"ignore"` |

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "ignore",
      "tag": "direct"
    }
  ]
}
```

## `urltest`

Use it when you have several links (for example two VPNs and your normal internet) and want keen-pbr to pick a working one automatically. keen-pbr periodically opens `url` through each link and measures the response time. In the Web UI the type is called **Auto-select (urltest)**.

If no link works, traffic for this outbound is blocked rather than leaking through your normal internet connection around the VPN.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"urltest"` |
| `url` | string | yes | URL used for availability and latency checks |
| `interval_ms` | integer | no (default: `180000`) | Interval between checks in milliseconds |
| `probe_timeout_ms` | integer | no (default: `5000`) | Timeout for one check in milliseconds |
| `tolerance_ms` | integer | no (default: `100`) | Do not switch to another link if it is faster than the current one by less than this many milliseconds |
| `strategy` | string | no (default: `"priority"`) | How to pick a link. In the Web UI: **Selection strategy**. `"priority"` (**Fastest**): all traffic goes through one link, the one with the lowest latency in the first working tier. `"balance"` (**Multipath**): new connections are spread across the working links by weight (not available on Keenetic) |
| `conntrack_on_switch` | string | no (default: `"preserve"`) | What to do with open connections when switching to another link. In the Web UI: **Existing connections on switch**. `"preserve"` (**Keep**) keeps them on the old link, `"delete"` (**Reset**) deletes their {{< term "conntrack" >}} entries so they reconnect. See [Failover]({{< relref "/docs/configuration/advanced#failover" >}}) |
| `outbound_groups` | array | yes | Ordered list of link groups, called tiers in the Web UI (see below) |
| `retry` | object | no | Check retries (see below) |
| `circuit_breaker` | object | no | Temporary exclusion of an unstable link (see below) |

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "urltest",
      "tag": "auto_select",
      "url": "https://www.gstatic.com/generate_204",
      "interval_ms": 180000,
      "probe_timeout_ms": 5000,
      "tolerance_ms": 100,
      "outbound_groups": [
        { "members": [{ "outbound": "vpn1" }, { "outbound": "vpn2" }] },
        { "members": [{ "outbound": "wan" }] }
      ],
      "retry": {
        "attempts": 3,
        "interval_ms": 1000
      },
      "circuit_breaker": {
        "failure_threshold": 5,
        "success_threshold": 2,
        "timeout_ms": 30000,
        "half_open_max_requests": 1
      }
    }
  ]
}
```

### Outbound Groups (tiers)

A group is a set of links to pick a working one from. You can have several groups, checked in order: the first is the primary one, the rest are backups. keen-pbr uses the first group that has at least one working link. A backup group is used only when no link in the groups above it works.

In the Web UI groups are called tiers: **Primary tier** and **Backup tier N**. Click **Add backup tier** to add a fallback.

| Field | Type | Required | Description |
|---|---|---|---|
| `members` | array of object | yes | Links in this group (see below) |

Each link in a group:

| Field | Type | Required | Description |
|---|---|---|---|
| `outbound` | string | yes | Tag of an `interface` or `table` outbound. You do not need a tier that blocks traffic: when no link works, traffic is blocked anyway |
| `weight` | integer 1-100 | no (default: `1`) | Link weight; in the Web UI, **Weight**. Used only with `strategy: "balance"`: the higher the weight, the more new connections the link gets. Links with weights 7 and 3 get 70% and 30% of new connections while both work. If one link is down, its share is split among the others. Connections that are already open are not moved |

```json { filename="config.json" }
"outbound_groups": [
  { "members": [ { "outbound": "wan1", "weight": 7 }, { "outbound": "wan2", "weight": 3 } ] },
  { "members": [ { "outbound": "backup" } ] }
]
```

### Multipath (`strategy: "balance"`)

This strategy spreads load across the working links of the first working tier. A link is chosen per new connection, not per request: the whole connection, including HTTP/2 and {{< term "QUIC" >}}, stays on one link. `tolerance_ms` and `conntrack_on_switch` are not used with balancing. When a link stops working, {{< term "conntrack" >}} entries of its connections are deleted, and new connections are spread across the remaining links.

With nftables, connections are split exactly by weight. With iptables, a link is picked at random with the right probability, so with only a few connections the shares can differ noticeably from the weights; with many connections they even out. iptables needs the `xt_statistic` kernel module; if it is missing, keen-pbr reports it when applying the configuration. This strategy is not available on Keenetic. Balancing is not available with `--use-raw-prerouting`.

{{< callout type="info" >}}
Balancing across several WANs needs {{< term "NAT" >}} on every uplink, loose `rp_filter`, and an {{< term "fwmark" >}} range that other software does not use. See [Multi-WAN Load Balancing]({{< relref "/docs/configuration/multi-wan" >}}) for a complete Debian/iptables setup.
{{< /callout >}}

### Retries (`retry`)

| Field | Type | Required | Description |
|---|---|---|---|
| `attempts` | integer | no (default: `1`) | How many times to run the check in total, including the first try, before treating it as failed |
| `interval_ms` | integer | no (default: `1000`) | Pause between attempts in milliseconds |

### Excluding an unstable link (`circuit_breaker`)

If a link fails several checks in a row, keen-pbr stops using it for a while. This keeps traffic from bouncing back and forth while a link keeps going up and down. The defaults suit most setups; you do not need to change them.

| Field | Type | Required | Description |
|---|---|---|---|
| `failure_threshold` | integer | no (default: `5`) | Failed checks in a row needed to exclude the link |
| `timeout_ms` | integer | no (default: `30000`) | After how many milliseconds to try the excluded link again |
| `success_threshold` | integer | no (default: `2`) | Successful checks in a row needed to bring the link back |
| `half_open_max_requests` | integer | no (default: `1`) | How many checks may run at once while an excluded link is being rechecked |

An excluded link is shown as **Unavailable** on the **Routing** > **Outbounds** page of the web UI and as `unavailable` in `GET /api/runtime/outbounds`.

## `icmptest`

Use it instead of `urltest` when HTTP checks do not work for you. In the Web UI the type is called **Auto-select (ICMP)**. A link is checked by pinging a specific address ({{< term "ICMP" >}} Echo). For every link in a group, set `target` (in the Web UI, **Ping target**): the IP address to ping through that link.

```json { filename="config.json" }
{
  "type": "icmptest",
  "tag": "auto_ping",
  "interval_ms": 60000,
  "count": 3,
  "max_failed": 0,
  "packet_interval_ms": 200,
  "probe_timeout_ms": 1000,
  "max_rtt_ms": 500,
  "tolerance_ms": 10,
  "outbound_groups": [
    {
      "members": [
        { "outbound": "vpn", "target": "1.1.1.1" },
        { "outbound": "wan", "target": "9.9.9.9", "weight": 3 }
      ]
    }
  ]
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `interval_ms` | integer | no (default: `60000`) | Interval between checks, from 1 second to 24 hours |
| `count` | integer | no (default: `3`) | Pings sent through each link per check, 1 to 10 |
| `max_failed` | integer | no (default: `0`) | How many of the `count` pings may be lost while the link still counts as working |
| `packet_interval_ms` | integer | no (default: `200`) | Pause between pings, 100 to 1000 ms |
| `probe_timeout_ms` | integer | no (default: `1000`) | How long to wait for one ping reply, 100 to 5000 ms |
| `max_rtt_ms` | integer | no (default: `500`) | A slower reply counts as lost. Must not exceed `probe_timeout_ms` |
| `outbound_groups[].members[].target` | string | yes | IPv4 or IPv6 address to ping through this link (**Ping target**) |

`tolerance_ms`, `strategy`, `conntrack_on_switch`, groups (tiers) and weights work the same as in `urltest`.

Pings are sent one after another, so checking all links takes time. `interval_ms` must exceed that time by at least 25%; otherwise keen-pbr rejects the configuration and tells you the minimum allowed value. Groups may contain at most 16 links in total.
