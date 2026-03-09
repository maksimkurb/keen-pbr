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
    "fallback": "wan"
  }
}
```

| Field | Type | Description |
|---|---|---|
| `rules` | array | Ordered list of route rules |
| `fallback` | string | Outbound tag for unmatched traffic |

## Route Rule Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `list` | array of string | yes | List names whose traffic this rule matches |
| `outbound` | string | yes | Outbound tag to route matched traffic through |
| `proto` | string | no | Protocol: `"tcp"`, `"udp"`, or `"tcp/udp"`. Omit for any. |
| `src_port` | string | no | Source port spec (see Port Syntax below) |
| `dest_port` | string | no | Destination port spec (see Port Syntax below) |
| `src_addr` | array of string | no | Source CIDR(s) to match (see Address Negation below) |
| `dest_addr` | array of string | no | Destination CIDR(s) to match (additional to the list) |

## Port Syntax

Port specs are a string with the following formats:

| Format | Example | Matches |
|---|---|---|
| Single port | `"443"` | port 443 |
| List | `"80,443"` | port 80 or 443 |
| Range | `"8000-9000"` | ports 8000 through 9000 |
| Negation | `"!443"` | all ports except 443 |
| Negated list | `"!80,443"` | all ports except 80 and 443 |

## Address Negation

`src_addr` and `dest_addr` accept an array of CIDR strings. All entries must either all be negated (prefixed with `!`) or none — mixing negated and non-negated entries in the same array is not supported.

| Format | Example | Matches |
|---|---|---|
| Normal | `["192.168.10.0/24"]` | traffic from this subnet |
| Negated | `["!192.168.1.0/24"]` | traffic from any source except this subnet |
| Multi-negated | `["!10.0.0.0/8", "!172.16.0.0/12"]` | traffic not from either subnet |

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
  "src_addr": ["192.168.20.0/24", "192.168.30.0/24"],
  "proto": "tcp",
  "dest_port": "443",
  "outbound": "vpn"
}
```

### Address filter — match a specific source subnet

```json
{
  "list": ["my-ips"],
  "src_addr": ["192.168.10.0/24"],
  "outbound": "vpn"
}
```

### Full filter — DNS from a subnet through VPN

```json
{
  "list": ["my-domains"],
  "src_addr": ["192.168.10.0/24"],
  "dest_addr": ["8.8.8.0/24"],
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
  "src_addr": ["!192.168.1.0/24"],
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

### Negation — block traffic NOT going to a trusted subnet

```json
{
  "list": ["my-ips"],
  "dest_addr": ["!10.0.0.0/8", "!172.16.0.0/12"],
  "outbound": "block"
}
```

{{< callout type="warning" >}}
Mixed negation in `src_addr` / `dest_addr` is not supported. All entries in the array must either all start with `!` or none of them should. For example, `["192.168.1.0/24", "!10.0.0.0/8"]` is invalid.
{{< /callout >}}
