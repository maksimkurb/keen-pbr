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
      "gateway": "10.8.0.1"
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
| `gateway` | string | no | Optional gateway IP address |

```json { filename="config.json" }
{
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1"
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

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"urltest"` |
| `url` | string | yes | URL used for availability and latency checks |
| `interval_ms` | integer | no (default: `180000`) | Interval between probes in milliseconds |
| `probe_timeout_ms` | integer | no (default: `5000`) | Timeout for each individual probe attempt in milliseconds |
| `tolerance_ms` | integer | no (default: `100`) | Latency tolerance in ms; prevent outbound switching if the latency difference between the current and new best outbound is less than this tolerance |
| `outbound_groups` | array | yes | Ordered list of outbound groups (see below) |
| `retry` | object | no | Retry configuration (see below) |
| `circuit_breaker` | object | no | Circuit breaker configuration (see below) |

### Outbound Groups

Groups are checked in order. Within the first healthy group, outbounds are selected by the lowest latency. If all outbounds in a group are unhealthy, the next group is evaluated. This lets you define more complex priority rules. For example, you can prefer one of two slower outbounds first, and if both are unavailable, fall back to another outbound that is faster but more expensive.

| Field | Type | Required | Description |
|---|---|---|---|
| `outbounds` | array of string | yes | Ordered list of outbound tags to try |

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
        { "outbounds": ["vpn1", "vpn2"] },
        { "outbounds": ["wan"] }
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
