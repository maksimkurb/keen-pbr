---
title: Route Rules
weight: 3
---

Route rules define which traffic is routed where. Rules are evaluated in order — the first match wins. Traffic that matches no rule is sent to the `fallback` outbound.

## Configuration

```json
{
  "route": {
    "rules": [...],
    "fallback": "ignore"
  }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `rules` | array | yes | Ordered list of route rules |
| `fallback` | string | no | Outbound tag for unmatched traffic |

## Route Rule Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `list` | array of string | yes | List names whose traffic this rule matches |
| `outbound` | string | yes | Outbound tag to route matched traffic through |
| `proto` | string | no | Protocol: `"tcp"`, `"udp"`, or `"tcp/udp"`. Omit for any. |
| `src_port` | string | no | Source port spec (see Address & Port Syntax below) |
| `dest_port` | string | no | Destination port spec (see Address & Port Syntax below) |
| `src_addr` | string | no | Source CIDR(s) to match (see Address & Port Syntax below) |
| `dest_addr` | string | no | Destination CIDR(s) to match (additional to the list) |

## Address & Port Syntax

`src_addr`, `dest_addr`, `src_port`, and `dest_port` all use the same string syntax:

| Format | Example | Matches |
|---|---|---|
| Single value | `"192.168.1.0/24"` | this subnet |
| List | `"192.168.1.0/24,10.0.0.0/8"` | either subnet |
| Negation | `"!192.168.1.0/24"` | any source except this subnet |
| Negated list | `"!192.168.1.0/24,10.0.0.0/8"` | any source except either subnet |
| Single port | `"443"` | port 443 |
| Port list | `"80,443"` | port 80 or 443 |
| Port range | `"8000-9000"` | ports 8000 through 9000 |
| Negated port | `"!443"` | all ports except 443 |
| Negated port list | `"!80,443"` | all ports except 80 and 443 |

A single `!` at the start negates the entire value. Negation applies to all comma-separated entries — mixing negated and non-negated entries is not possible by design.

## Examples

### Basic — route a list through VPN

```json
{
  "list": ["my-domains", "my-ips", "remote-list"],
  "outbound": "vpn"
}
```

### Port filter — only HTTPS TCP from a subnet

```json
{
  "list": ["my-domains"],
  "src_addr": "192.168.20.0/24,192.168.30.0/24",
  "proto": "tcp",
  "dest_port": "443",
  "outbound": "vpn"
}
```

### Address filter — match a specific source subnet

```json
{
  "list": ["my-ips"],
  "src_addr": "192.168.10.0/24",
  "outbound": "vpn"
}
```

### Full filter — DNS from a subnet through VPN

```json
{
  "list": ["my-domains"],
  "src_addr": "192.168.10.0/24",
  "dest_addr": "8.8.8.0/24",
  "proto": "udp",
  "src_port": "1024-65535",
  "dest_port": "53",
  "outbound": "vpn"
}
```

### Negation — all sources except local LAN

```json
{
  "list": ["my-ips"],
  "src_addr": "!192.168.1.0/24",
  "outbound": "vpn"
}
```

### Negation — all TCP except HTTPS goes through VPN

```json
{
  "list": ["my-domains"],
  "proto": "tcp",
  "dest_port": "!443",
  "outbound": "vpn"
}
```

### Negation — all UDP except DNS and NTP goes through VPN

```json
{
  "list": ["my-domains"],
  "proto": "udp",
  "dest_port": "!53,123",
  "outbound": "vpn"
}
```

### Negation — block traffic NOT going to a trusted subnet

```json
{
  "list": ["my-ips"],
  "dest_addr": "!10.0.0.0/8,172.16.0.0/12",
  "outbound": "block"
}
```
