---
title: Outbounds
weight: 1
---

Outbounds tell keen-pbr where matching traffic should go.

Every outbound `tag` must match `^[a-z][a-z0-9_]*$` and be at most 24 characters.

Most users only need:

- one `interface` outbound for the VPN connection
- one `interface` outbound for the normal connection, often `wan`
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
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      "gateway": "192.168.1.1"
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

If neither `gateway` nor `gateway6` is set, keen-pbr creates both IPv4 and IPv6 default routes for the interface outbound.

If only one gateway is set, keen-pbr creates a routed default for that address family and an `unreachable` default for the other family so marked traffic cannot leak outside the outbound table.

If both `gateway` and `gateway6` are set, keen-pbr creates distinct IPv4 and IPv6 default routes.

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

Use this only if another service on the system already created a separate routing table for you and you want keen-pbr to reuse it.

To find existing table IDs before choosing one, inspect the current policy routing state and routes. To see table name mappings, check `/etc/iproute2/rt_tables`:

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

Passes traffic through without any routing modification. Use this to explicitly exclude traffic from other rules.

When a route rule resolves to an `ignore` outbound, keen-pbr installs a matching firewall pass-through verdict for that traffic. This stops further keen-pbr rule processing without setting a mark or dropping the packet. No routing table or `ip rule` is created for that match, so the packet follows the system's normal routing. Since route rules are evaluated top to bottom and the first match wins, `ignore` is most useful for exception rules placed before broader catch-all rules.

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

Use this when you have several candidate outbounds and want keen-pbr to automatically pick the best available one.

With the default `priority` strategy, firewall rules keep the `urltest`
outbound's stable mark. Its policy rule points at the selected child's routing
table, including an existing table used by a `table` outbound; firewall rules
are not rebuilt on selection changes. `balance` keeps that stable mark for
internal detours, but refreshes its balance classifier after each probe sweep.

keen-pbr always appends terminal IPv4 and IPv6 `unreachable` default routes to
the generated `urltest` fallback table. This table is selected when no child is
usable, while a terminal policy rule prevents a selected child table from
falling through to normal routing.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"urltest"` |
| `url` | string | yes | URL used for availability and latency checks |
| `interval_ms` | integer | no (default: `180000`) | Interval between probes in milliseconds |
| `probe_timeout_ms` | integer | no (default: `5000`) | Timeout for each individual probe attempt in milliseconds |
| `tolerance_ms` | integer | no (default: `100`) | Latency tolerance in ms; prevent outbound switching if the latency difference between the current and new best outbound is less than this tolerance |
| `strategy` | string | no (default: `"priority"`) | `"priority"` selects one child as before. `"balance"` (nftables, or iptables with `xt_statistic`; not on Keenetic) distributes new connections over the usable members of the first healthy group, in proportion to their `weight` (equally by default). |
| `outbound_groups` | array | yes | Ordered list of outbound groups (see below) |
| `retry` | object | no | Retry configuration (see below) |
| `circuit_breaker` | object | no | Circuit breaker configuration (see below) |

### Outbound Groups

`outbound_groups` is an ordered list of steps: the first group is tried first.
Within the first healthy group, `priority` selects by latency. If all members of
a group are unhealthy, the next group is evaluated.

| Field | Type | Required | Description |
|---|---|---|---|
| `members` | array of object | yes | Ordered members of this group (see below) |

Each member:

| Field | Type | Required | Description |
|---|---|---|---|
| `outbound` | string | yes | Outbound tag (interface, table or blackhole) |
| `weight` | integer 1-100 | no (default: `1`) | Share of new connections in the `balance` strategy; ignored by `priority`. Members with weights 7 and 3 get 70% and 30% of new connections while both are usable. |

```json { filename="config.json" }
"outbound_groups": [
  { "members": [ { "outbound": "wan1", "weight": 7 }, { "outbound": "wan2", "weight": 3 } ] },
  { "members": [ { "outbound": "backup" } ] }
]
```

{{< callout type="info" >}}
The older form (`outbounds: ["a", "b"]` plus a group-level `weight` as step
priority, lower first) is still read. When the daemon loads a config that uses
it, the file is rewritten in the `members` form (groups ordered by their old
`weight`) after it validated successfully, and the original is kept once as
`config.json.bak-pre-members` next to it. Comments in the original file are not
carried over (they stay in the backup). Mixing `members` with the old fields in
one group is rejected.
{{< /callout >}}

With `strategy: "balance"`, selection is per connection, not per HTTP request.
HTTP/2 and QUIC multiplexed traffic stays on the WAN chosen for that connection.
Each usable member receives `weight / sum of the usable weights` of new
connections. `tolerance_ms`
applies only to `priority` selection. For balance groups, `conntrack_on_switch`
is ignored; only a failed child's conntrack flows are removed when it becomes
unhealthy. When no candidate is usable, the test group's existing terminal
fallback blocks marked traffic rather than leaking to the main table.

Balancing works on both firewall backends. nftables alternates new connections
with `numgen inc` and a verdict map. iptables has no counter match that can be
reloaded cheaply, so it picks a candidate at random: a cascade of
`-m statistic --mode random --probability w1/(w1+...+wn)`, `w2/(w2+...+wn)`, ...
rules (the last candidate unconditional; with equal weights `1/n`, `1/(n-1)`, ...)
in the mangle classification chain, which gives every usable candidate its
weighted share. Over a few connections the split is therefore only statistical
on iptables, while nftables walks a `numgen inc` counter over weight-sized slot
ranges and is exactly proportional. On both
backends the chosen mark is saved to the connection (`CONNMARK`) and restored
for the rest of the flow, so only new connections are balanced. iptables
balancing needs the `xt_statistic` kernel module; if it cannot be used, applying
the firewall fails with an error naming the module. It is not available with
`--use-raw-prerouting` (raw runs before conntrack, so a choice could not be
kept per connection).

{{< callout type="info" >}}
Balancing across several WANs needs NAT on every uplink, loose `rp_filter`, and
an fwmark range that other services do not touch. See
[Multi-WAN Load Balancing](../multi-wan/) for a complete Debian/iptables setup.
{{< /callout >}}

### Retry Configuration

| Field | Type | Required | Description |
|---|---|---|---|
| `attempts` | integer | no (default: `3`) | Number of probe attempts before marking outbound as failed |
| `interval_ms` | integer | no (default: `1000`) | Delay between retry attempts in milliseconds |

### Circuit Breaker Configuration

| Field | Type | Required | Description |
|---|---|---|---|
| `failure_threshold` | integer | no (default: `5`) | Consecutive failures before opening the circuit |
| `success_threshold` | integer | no (default: `2`) | Consecutive successes in half-open state to close the circuit |
| `timeout_ms` | integer | no (default: `30000`) | Time before transitioning from open to half-open state |
| `half_open_max_requests` | integer | no (default: `1`) | Max probe requests allowed in half-open state |

**Circuit breaker states:**
- `closed` — healthy, traffic passes through normally
- `open` — failed, traffic blocked during cooldown period
- `half_open` — testing recovery with limited probe requests

## `icmptest`

`icmptest` selects candidates like `urltest`, but sends ICMP Echo packets through
each candidate's fwmark. Every candidate needs one explicit literal IPv4 or IPv6
destination in its group member. It supports the same `priority` (default) and
`balance` strategies.

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

The older `outbound_groups[].candidates` form and the even older
`outbound_groups[].outbounds` plus top-level `probes` form are still accepted.
They are converted to `members` when the configuration is loaded, and the daemon
rewrites the file in the new form after a successful validation (original kept
as `<config>.bak-pre-members`). Mixing legacy and canonical fields is rejected.

Attempts are sequential. After one reply or timeout is fully processed, the
daemon waits `packet_interval_ms` before sending the next request. Therefore the
worst case for one candidate is
`count * probe_timeout_ms + (count - 1) * packet_interval_ms`. The configured
`interval_ms` must cover all candidates plus a 25% reserve.

Replies are matched by target address, ICMP type, identifier, and sequence.
Unrelated or late replies are ignored until the current attempt's deadline. A
matched reply over `max_rtt_ms` counts as failed, but remains visible in runtime
packet statistics.

The server limits a run to 1–10 packets per candidate, 1–16 unique candidates,
160 packets total, and a worst-case sweep of 10 minutes. The pause must be
100–1000 ms, the reply timeout 100–5000 ms, and the sweep interval between one
second and 24 hours. Datagram ICMP sockets require the daemon account to be
allowed by the platform's ping-socket policy (or have the corresponding network
capability).

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
