# Outbounds

Outbounds tell keen-pbr where matching traffic should go.

An outbound `tag` uses lowercase Latin letters, digits and `_`, starts with a letter, and is at most 24 characters long (`^[a-z][a-z0-9_]*$`).

Most users only need:

- one `interface` outbound for the VPN connection
- one `table` outbound with `table: 254` (the system's main routing table) for the normal connection, often `wan`
- optionally one `urltest` outbound if they want automatic failover between several connections

If you are not sure, start with `interface` outbounds and come back to the others later.

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

Use this when you want to send traffic through a specific connection such as a VPN tunnel or your normal internet uplink.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"interface"` |
| `interface` | string | yes | Egress network interface name (e.g. `tun0`) |
| `gateway` | string | no | Optional IPv4 gateway address |
| `gateway6` | string | no | Optional IPv6 gateway address |

For tunnels such as `tun0` or `wg0`, a gateway is usually not needed: without `gateway` and `gateway6`, both IPv4 and IPv6 traffic is sent straight into the interface. Set a gateway when the next hop has to be named, for example a second WAN behind the ISP's router.

If you set only one of them, traffic of the other IP version for this outbound is blocked rather than leaking past the VPN.

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

Sends traffic through an existing routing table: usually `254`, the system's main table (the normal internet connection), or a table another service has created.

To see which tables exist and their names, run:

```bash {filename="bash"}
ip rule show
cat /etc/iproute2/rt_tables
```

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"table"` |
| `table` | integer | yes | Existing routing table number |

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

Drops all matching traffic. Useful for blocking access to specific resources.

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

Leaves traffic alone: keen-pbr does not touch it, and it follows the router's normal routing as if keen-pbr were not there.

Use it for exceptions. Rules are checked from top to bottom and the first match wins, so put the `ignore` rule above a broader one. For example, the whole `streaming` list goes through the VPN except addresses from the `streaming_direct` list (`direct` is the `ignore` outbound from the example below):

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

Use this when you have several links (for example two VPNs and your normal internet) and want keen-pbr to pick a working one automatically. keen-pbr periodically opens `url` through each link and measures the response time.

If no link works, traffic for this outbound is blocked rather than leaking through your normal internet connection around the VPN.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"urltest"` |
| `url` | string | yes | URL used for availability and latency checks |
| `interval_ms` | integer | no (default: `180000`) | Interval between checks in milliseconds |
| `probe_timeout_ms` | integer | no (default: `5000`) | Timeout for one check in milliseconds |
| `tolerance_ms` | integer | no (default: `100`) | Do not switch to another link if it is faster than the current one by less than this many milliseconds |
| `strategy` | string | no (default: `"priority"`) | `"priority"`: all traffic goes through the single fastest link. `"balance"`: new connections are spread across the working links of the first working group in proportion to `weight` (not available on Keenetic) |
| `conntrack_on_switch` | string | no (default: `"preserve"`) | What to do with open connections when moving to a faster link: `"preserve"` keeps them on the old link, `"delete"` deletes their {{< term "conntrack" >}} entries so they reconnect. See [Failover]({{< relref "/docs/configuration/advanced#failover" >}}) |
| `outbound_groups` | array | yes | Ordered list of link groups (see below) |
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

### Outbound Groups

`outbound_groups` is a list of groups in priority order. keen-pbr uses the first group that has at least one working link. If no link in a group works, the next group is used.

| Field | Type | Required | Description |
|---|---|---|---|
| `members` | array of object | yes | Links in this group (see below) |

Each link in a group:

| Field | Type | Required | Description |
|---|---|---|---|
| `outbound` | string | yes | Outbound tag (`interface`, `table` or `blackhole`) |
| `weight` | integer 1-100 | no (default: `1`) | Share of new connections with `strategy: "balance"`; ignored by `priority`. Links with weights 7 and 3 get 70% and 30% of new connections while both work |

```json { filename="config.json" }
"outbound_groups": [
  { "members": [ { "outbound": "wan1", "weight": 7 }, { "outbound": "wan2", "weight": 3 } ] },
  { "members": [ { "outbound": "backup" } ] }
]
```

{{< callout type="info" >}}
The older group format (`outbounds: ["a", "b"]` with a group-level `weight` as its priority) is still supported. On the first start keen-pbr rewrites `config.json` in the new format and keeps the old file next to it as `config.json.bak-pre-members`. Comments from the old file are not carried over. Old and new fields cannot be mixed in one group.
{{< /callout >}}

### Load balancing (`strategy: "balance"`)

A link is chosen per new connection, not per request: the whole connection, including HTTP/2 and {{< term "QUIC" >}}, stays on one link. `tolerance_ms` and `conntrack_on_switch` are not used with balancing. When a link stops working, {{< term "conntrack" >}} entries of its connections are deleted, and new connections are spread across the remaining links.

With nftables, connections are split exactly by weight. With iptables, a link is picked at random with the right probability, so with only a few connections the shares can differ noticeably from the weights; with many connections they even out. iptables needs the `xt_statistic` kernel module; if it is missing, keen-pbr reports it when applying the configuration. Balancing is not available with `--use-raw-prerouting`.

{{< callout type="info" >}}
Balancing across several WANs needs {{< term "NAT" >}} on every uplink, loose `rp_filter`, and an {{< term "fwmark" >}} range that other software does not use. See [Multi-WAN Load Balancing]({{< relref "/docs/configuration/multi-wan" >}}) for a complete Debian/iptables setup.
{{< /callout >}}

### Retries (`retry`)

| Field | Type | Required | Description |
|---|---|---|---|
| `attempts` | integer | no (default: `3`) | How many times to retry a check before treating the link as down |
| `interval_ms` | integer | no (default: `1000`) | Pause between attempts in milliseconds |

### Excluding an unstable link (`circuit_breaker`)

If a link fails several checks in a row, keen-pbr stops using it for a while. This keeps traffic from bouncing back and forth while a link keeps going up and down.

| Field | Type | Required | Description |
|---|---|---|---|
| `failure_threshold` | integer | no (default: `5`) | Failed checks in a row needed to exclude the link |
| `timeout_ms` | integer | no (default: `30000`) | After how many milliseconds to try the excluded link again |
| `success_threshold` | integer | no (default: `2`) | Successful checks in a row needed to bring the link back |
| `half_open_max_requests` | integer | no (default: `1`) | How many checks may run at once while an excluded link is being rechecked |

An excluded link is shown as **Unavailable** on the **Outbounds** page of the web UI and as `unavailable` in `GET /api/runtime/outbounds`.

## `icmptest`

Same as `urltest`, but links are checked with ping ({{< term "ICMP" >}} Echo) instead of an HTTP request. For every link in a group, set `target`: the IP address to ping through that link.

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
| `outbound_groups[].members[].target` | string | yes | IPv4 or IPv6 address to ping through this link |

`tolerance_ms`, `strategy`, `conntrack_on_switch`, groups and weights work the same as in `urltest`.

Pings are sent one after another, so checking all links takes time. `interval_ms` must exceed that time by at least 25%; otherwise keen-pbr rejects the configuration and tells you the minimum allowed value. Groups may contain at most 16 links in total.

