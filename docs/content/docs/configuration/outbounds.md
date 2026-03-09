---
title: Outbounds
weight: 1
---

Outbounds define where traffic goes. Each outbound has a unique `tag` and a `type`. The `type` determines which fields are required.

## `interface`

Routes traffic via a specific network interface, optionally through a gateway.

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

Routes traffic using an existing kernel routing table. The table must already be populated (e.g. by a VPN client or manual `ip route` commands).

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"table"` |
| `table` | integer | yes | Kernel routing table ID |

```json
{
  "type": "table",
  "tag": "custom-table",
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

Probes multiple child outbounds via HTTP and automatically selects the best one based on latency. Includes retry logic and a circuit breaker to prevent flapping.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier |
| `type` | string | yes | `"urltest"` |
| `url` | string | yes | HTTP URL used for latency probes |
| `interval_ms` | integer | no (default: `180000`) | Interval between probes in milliseconds |
| `tolerance_ms` | integer | no (default: `100`) | Latency tolerance in ms; outbounds within this range of the best are considered equivalent |
| `outbound_groups` | array | yes | Ordered list of outbound groups (see below) |
| `retry` | object | no | Retry configuration (see below) |
| `circuit_breaker` | object | no | Circuit breaker configuration (see below) |

### Outbound Groups

Groups are evaluated in order. Within a healthy group, outbounds are selected by `weight`.

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
  "tag": "auto-select",
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
