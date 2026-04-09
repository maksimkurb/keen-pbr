---
title: Outbounds
weight: 1
---

Outbounds tell keen-pbr where matching traffic should go.

Most users only need:

- one `interface` outbound for the VPN connection
- one `interface` outbound for the normal connection, often `wan`
- optionally one `urltest` outbound if they want automatic failover between several connections

If you are not sure, start with `interface` outbounds and come back to the others later.

## Common Example

```json
[
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

```json
{
  "type": "interface",
  "tag": "vpn",
  "interface": "tun0",
  "gateway": "10.8.0.1"
}
```

## `table`

Use this only if another service on the system already created a separate routing table for you and you want keen-pbr to reuse it.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"table"` |
| `table` | integer | yes | Existing routing table number |

```json
{
  "type": "table",
  "tag": "custom_table",
  "table": 200
}
```

## `blackhole`

Drops all matching traffic. Useful for blocking access to specific lists.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"blackhole"` |

```json
{
  "type": "blackhole",
  "tag": "block"
}
```

## `ignore`

Passes traffic through without any routing modification. Use this to explicitly exclude traffic from other rules.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"ignore"` |

```json
{
  "type": "ignore",
  "tag": "direct"
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
| `tolerance_ms` | integer | no (default: `100`) | Latency tolerance in ms; outbounds within this range of the best are considered equivalent |
| `outbound_groups` | array | yes | Ordered list of outbound groups (see below) |
| `retry` | object | no | Retry configuration (see below) |
| `circuit_breaker` | object | no | Circuit breaker configuration (see below) |

### Outbound Groups

Groups are checked in order. Within a healthy group, outbounds are selected by `weight`.

| Field | Type | Required | Description |
|---|---|---|---|
| `outbounds` | array of string | yes | Ordered list of outbound tags to try |
| `weight` | integer | no (default: `1`) | Relative selection weight when multiple groups are healthy |

### Retry Configuration

| Field | Type | Required | Description |
|---|---|---|---|
| `attempts` | integer | no (default: `3`) | Number of probe attempts before marking as failed |
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

```json
{
  "type": "urltest",
  "tag": "auto_select",
  "url": "https://www.gstatic.com/generate_204",
  "interval_ms": 180000,
  "tolerance_ms": 100,
  "outbound_groups": [
    { "weight": 1, "outbounds": ["vpn"] },
    { "weight": 2, "outbounds": ["wan"] }
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
```
