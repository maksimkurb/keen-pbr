---
title: Route Rules
weight: 3
---

Route rules connect your lists to your outbounds.

Most users only need one simple rule for each list, for example:

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_sites"],
        "outbound": "vpn"
      }
    ]
  }
}
```

Rules are checked from top to bottom. The first match wins. Traffic that matches no rule is left unmarked and follows normal system routing.

## Configuration

```json { filename="config.json" }
{
  "route": {
    "inbound_interfaces": [...],
    "rules": [...]
  }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `inbound_interfaces` | array of string | no | Optional ingress-interface filter. If omitted or empty, all interfaces are eligible. If non-empty, only packets entering via listed interfaces are processed by KeenPbrTable. |
| `rules` | array | yes | Ordered list of route rules |

## Route Rule Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `enabled` | boolean | no | Whether this rule is active. `false` disables it. `true`, omitted, or `null` all mean enabled. |
| `list` | array of string | yes | List names whose traffic this rule matches |
| `outbound` | string | yes | Outbound tag to route matched traffic through |
| `proto` | string | no | Protocol: `"tcp"`, `"udp"`, or `"tcp/udp"` |
| `dscp` | integer | no | DSCP tag value from `1` to `63` |
| `src_port` | string | no | Match only specific source ports |
| `dest_port` | string | no | Match only specific destination ports |
| `src_addr` | string | no | Match only specific source addresses |
| `dest_addr` | string | no | Match only specific destination addresses |

## Examples

### Basic — route a list through VPN

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "enabled": true,
        "list": ["my_domains", "my_ips", "remote_list"],
        "outbound": "vpn"
      }
    ]
  }
}
```

### Disable a rule temporarily without deleting it

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "enabled": false,
        "list": ["my_domains"],
        "outbound": "vpn"
      }
    ]
  }
}
```

When `enabled` is omitted or set to `null`, the rule is treated as enabled.

### Ingress interface filter — process only packets from `br0`

```json { filename="config.json" }
{
  "route": {
    "inbound_interfaces": ["br0"],
    "rules": [
      {
        "list": ["my_domains"],
        "outbound": "vpn"
      }
    ]
  }
}
```

### Ingress interface filter disabled (omitted or empty)

If `inbound_interfaces` is omitted (default) or explicitly set to an empty array, interface filtering is disabled and route rules are applied regardless of ingress interface.

```json { filename="config.json" }
{
  "route": {
    "inbound_interfaces": [],
    "rules": [
      {
        "list": ["my_domains"],
        "outbound": "vpn"
      }
    ]
  }
}
```

### Port filter — only HTTPS TCP from two subnets

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_domains"],
        "src_addr": "192.168.20.0/24,192.168.30.0/24",
        "proto": "tcp",
        "dest_port": "443",
        "outbound": "vpn"
      }
    ]
  }
}
```

### DSCP filter — route tagged traffic through VPN

DSCP (Differentiated Services Code Point) is a 6-bit tag in the IP header. It is usually used for QoS, but keen-pbr can also use it as a routing selector: tag packets on the client device, then match that tag in a route rule. This is useful for per-application routing when the app does not have stable destination domains, IPs, or ports.

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "dscp": 46,
        "outbound": "vpn"
      }
    ]
  }
}
```

DSCP can be combined with the same list, protocol, address, and port filters as other route rules. For example, `"dscp": 46` plus `"src_addr": "192.168.10.20"` routes only tagged packets from that host.

The DSCP mark must already be present when the packet reaches the router. Configure it on the device that runs the application.

#### Windows: tag one application

Run PowerShell as Administrator:

```powershell
New-NetQosPolicy -Name "keen-pbr Zoom DSCP" -AppPathNameMatchCondition "zoom.exe" -DSCPAction 46
```

`AppPathNameMatchCondition` can be an executable name such as `zoom.exe` or a full application path. Windows then tags matching application traffic with DSCP `46`, and keen-pbr can route it with a rule containing `"dscp": 46`.

#### Linux: tag traffic from a user

On Linux, you can tag packets created by a specific local user. Replace `john` in the examples below with the user that runs the application you want to route.

With nftables:

```sh
sudo nft add table inet pbr_dscp
sudo nft 'add chain inet pbr_dscp output { type route hook output priority mangle; policy accept; }'
sudo nft add rule inet pbr_dscp output meta skuid john ip dscp set 46
sudo nft add rule inet pbr_dscp output meta skuid john ip6 dscp set 46
```

With iptables:

```sh
sudo iptables -t mangle -A OUTPUT -m owner --uid-owner john -j DSCP --set-dscp 46
sudo ip6tables -t mangle -A OUTPUT -m owner --uid-owner john -j DSCP --set-dscp 46
```

The iptables `owner` matcher applies to locally generated packets, not forwarded packets. For DSCP routing from a Linux laptop or PC, add these rules on that laptop or PC, not on the keen-pbr router.

### Address filter — match a specific source subnet

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_ips"],
        "src_addr": "192.168.10.0/24",
        "outbound": "vpn"
      }
    ]
  }
}
```

### Full filter — Google DNS from a subnet through VPN

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_domains"],
        "src_addr": "192.168.10.0/24",
        "dest_addr": "8.8.8.8",
        "dest_port": "53",
        "outbound": "vpn"
      }
    ]
  }
}
```

### Negation — all sources except local LAN

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_ips"],
        "src_addr": "!192.168.1.0/24",
        "outbound": "vpn"
      }
    ]
  }
}
```

### Negation — all TCP except HTTPS goes through VPN

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_domains"],
        "proto": "tcp",
        "dest_port": "!443",
        "outbound": "vpn"
      }
    ]
  }
}
```

### Negation — all UDP except DNS and NTP goes through VPN

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_domains"],
        "proto": "udp",
        "dest_port": "!53,123",
        "outbound": "vpn"
      }
    ]
  }
}
```

{{< callout type="warning" >}}
Keep in mind that DNS is working over TCP and UDP, but this rule covers only UDP. This rule is shown just for your reference, in real life you would probably like to add TCP rule for port 53 as well.
{{< /callout >}}

### Negation — block traffic NOT going to a trusted subnet

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_ips"],
        "dest_addr": "!10.0.0.0/8,172.16.0.0/12",
        "outbound": "block"
      }
    ]
  }
}
```

{{% details title="Advanced filters: address and port syntax" closed="true" %}}
`src_addr`, `dest_addr`, `src_port`, and `dest_port` all use the same string syntax:

| Format | Example | Matches |
|---|---|---|
| Single value | `"192.168.1.0/24"` | this subnet |
| List | `"192.168.1.0/24,8.8.8.8"` | either subnet/ip |
| Negation | `"!192.168.1.0/24"` | any source except this subnet |
| Negated list | `"!192.168.1.0/24,8.8.8.8"` | any source except either subnet |
| Single port | `"443"` | port 443 |
| Port list | `"80,443"` | port 80 or 443 |
| Port range | `"8000-9000"` | ports 8000 through 9000 |
| Negated port | `"!443"` | all ports except 443 |
| Negated port list | `"!80,443"` | all ports except 80 and 443 |

A single `!` at the start negates the entire value. Negation applies to all comma-separated entries.
{{% /details %}}
