---
title: Route Rules
weight: 3
---

A route rule links a condition ("traffic to sites from this list") with an outbound ("send it through this channel"). An outbound is a channel for traffic: a VPN, your ISP connection or a block; see [Outbounds]({{< relref "/docs/configuration/outbounds" >}}).

**In the Web UI:** open **Routing** > **Routing rules** and click **Add routing rule**. Pick a **Rule type**, set the conditions and the **Outbound**, then click **Apply** in the draft bar. To turn a rule off temporarily, clear the **Enable rule** switch.

Most users need one simple rule per list, for example:

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

Rules are checked from top to bottom, and the first match wins. Traffic that matches no rule follows the router's normal routing.

## Default local networks rule

Every example configuration starts its rule list with a rule that keeps local and special networks out of tunnels:

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["local_networks"],
        "outbound": "direct_local"
      },
      // ... your custom rules go here
    ]
  }
}
```

The `local_networks` list holds reserved address ranges that always use local routing:
- Link-local addresses: `169.254.0.0/16`, `fe80::/10`
- Loopback: `127.0.0.0/8`, `::1/128`
- Private networks: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`
- Unique local addresses: `fc00::/7`
- Special use ranges: `0.0.0.0/8`, `100.64.0.0/10`, `224.0.0.0/4`, `240.0.0.0/4`, `ff00::/8`

`direct_local` is an `ignore` outbound: traffic to these networks follows the router's normal routing.

**Why you need it:** without this rule, a rule for all addresses such as `"dest_addr": "0.0.0.0/0"` would also send traffic to local addresses into the tunnel, and the router or other devices on your network may become unreachable. Rules with `default_gateway` skip these addresses on their own.

**If your private network should go through a tunnel** (for example a corporate 10.x or 172.16.x network behind a VPN), add a rule for it *above* the `local_networks` rule:

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "dest_addr": "10.0.0.0/8",
        "outbound": "vpn"
      },
      {
        "list": ["local_networks"],
        "outbound": "direct_local"
      },
      // ... rest of your rules
    ]
  }
}
```

## Ingress interface filter

By default rules apply to traffic from all interfaces. To process only traffic from some networks (for example only the LAN, `br0`), list them in `inbound_interfaces`. In the Web UI: **General** > **Settings** > **General** > **Processed (inbound) interfaces**.

```json { filename="config.json" }
{
  "route": {
    "inbound_interfaces": ["br0"],
    "rules": [...]
  }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `inbound_interfaces` | array of string | no | Interfaces whose traffic is processed. If omitted or empty, all interfaces are processed |
| `rules` | array | yes | Ordered list of route rules |

The router's own traffic is processed only if **General** > **Settings** > **General** > **Process router's own traffic** (shown in **Advanced** settings mode) (`iproute.process_router_traffic`) is on. Do not list the interface the router uses to reach the internet in `inbound_interfaces`: the router's own traffic would then not be routed.

keen-pbr never routes DHCP (UDP ports 67–68 for IPv4 and 546–547 for IPv6), so devices always get their addresses from the router.

## Route Rule Fields

In the rule form in the Web UI you can add these conditions: **Lists**, **Protocol**, **DSCP**, **Source IP**, **Destination IP**, **Source port**, **Destination port**. All conditions you set must match at the same time.

| Field | Type | Required | Description |
|---|---|---|---|
| `enabled` | boolean | no | Whether the rule is active. `false` disables it; `true`, an omitted field and `null` mean enabled. In the Web UI: **Enable rule** |
| `list` | array of string | no | List names whose traffic this rule matches (**Lists**). Omit it when another condition is present; every rule needs at least one condition |
| `outbound` | string | yes | Outbound tag to route matched traffic through (**Outbound**) |
| `proto` | string | no | Protocol: `"tcp"`, `"udp"`, or `"tcp/udp"` (**Protocol**) |
| `dscp` | integer | no | {{< term "DSCP" >}} tag value from `1` to `63` (**DSCP**) |
| `src_port` | string | no | Only these source ports (**Source port**) |
| `dest_port` | string | no | Only these destination ports (**Destination port**) |
| `src_addr` | string | no | Only these source addresses: the devices or subnets the traffic comes from (**Source addresses**) |
| `dest_addr` | string | no | Only these destination addresses (**Destination addresses**) |
| `default_gateway` | `"ipv4"` or `"ipv6"` | no | {{< only openwrt debian >}} All internet traffic that would otherwise go through the router's main gateway (except local network addresses). In the Web UI: **Rule type** > **IPv4 default gateway** / **IPv6 default gateway**. Put these rules last |

## Failover or balancing for all internet traffic {#failover-or-balancing-for-all-internet-traffic}

Use this to send all internet traffic through an auto-select outbound (`urltest` or `icmptest`): if the main link fails, traffic switches to a backup, and with `balance` it is spread across several links.

{{< only openwrt debian >}}\
Add one `default_gateway` rule for IPv4 and one for IPv6 at the end of the rule list. Addresses on your local network keep normal routing.

By default these rules apply only to traffic from devices on your network. To include the router's own traffic, turn on **General** > **Settings** > **General** > **Process router's own traffic** (`iproute.process_router_traffic: true`).

```json { filename="config.json" }
{
  "iproute": { "process_router_traffic": true },
  "route": { "rules": [
    { "default_gateway": "ipv4", "outbound": "auto" },
    { "default_gateway": "ipv6", "outbound": "auto" }
  ] }
}
```

## Examples

### A list through the VPN

The most common case: sites and IPs from your lists go through the VPN.

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["my_domains", "my_ips", "remote_list"],
        "outbound": "vpn"
      }
    ]
  }
}
```

### An exception to a broader rule

Rules are checked from top to bottom, so put the exception above the broader rule. Here the whole `streaming` list goes through the VPN except addresses from the `streaming_direct` list. The `direct` outbound has the `ignore` type (traffic takes its usual path).

```json { filename="config.json" }
{
  "route": {
    "rules": [
      { "list": ["streaming_direct"], "outbound": "direct" },
      { "list": ["streaming"], "outbound": "vpn" }
    ]
  }
}
```

### Blocking a list

The `block` outbound has the `blackhole` type: traffic to addresses from the list is dropped.

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "list": ["ads"],
        "outbound": "block"
      }
    ]
  }
}
```

### One device or subnet

All traffic from the device `192.168.10.20` goes through the VPN. To pick a whole subnet, use it instead: `192.168.10.0/24`.

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "src_addr": "192.168.10.20",
        "outbound": "vpn"
      }
    ]
  }
}
```

Conditions can be combined: the rule below sends only addresses from the `my_ips` list through the VPN, and only from devices in `192.168.10.0/24`.

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

### Disabling a rule temporarily

Set `"enabled": false` (in the Web UI, clear **Enable rule**) and the rule stops working but stays in the config.

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

### Ports and protocol: HTTPS from two subnets

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

### All conditions at once: DNS queries to Google DNS from a subnet

The rule matches only if all conditions hold: the query comes from `192.168.10.0/24`, goes to `8.8.8.8`, on port 53.

```json { filename="config.json" }
{
  "route": {
    "rules": [
      {
        "src_addr": "192.168.10.0/24",
        "dest_addr": "8.8.8.8",
        "dest_port": "53",
        "outbound": "vpn"
      }
    ]
  }
}
```

### DSCP filter — route tagged traffic through VPN

{{< term "DSCP" >}} (Differentiated Services Code Point) is a 6-bit tag in the IP header. It is usually used for {{< term "QoS" >}}, but keen-pbr can also use it as a routing selector: tag packets on the client device, then match that tag in a route rule. This is useful for per-application routing when the app does not have stable destination domains, IPs, or ports.

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

`AppPathNameMatchCondition` can be an executable name such as `zoom.exe` or a full application path. Windows then tags matching application traffic with {{< term "DSCP" >}} `46`, and keen-pbr can route it with a rule containing `"dscp": 46`.

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

`owner` only sees packets created on the same computer, so add these rules on the Linux laptop or PC that runs the application, not on the keen-pbr router.

### Exclusions with `!`: all sources except the local LAN

`!` means "everything except". The rule below sends addresses from `my_ips` through the VPN unless the request comes from `192.168.1.0/24`.

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

### Exclusions with `!`: TCP traffic to addresses from a list, except HTTPS

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

### Exclusions with `!`: UDP traffic to addresses from a list, except DNS and NTP

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
DNS runs over both TCP and UDP, and this rule covers only UDP. The example is for reference: in practice you usually need the same rule for TCP port 53.
{{< /callout >}}

### Exclusions with `!`: block everything from a list except trusted subnets

Addresses from the `my_ips` list are blocked unless they are inside `10.0.0.0/8` or `172.16.0.0/12`.

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
| List | `"192.168.1.0/24,8.8.8.8"` | any of the listed addresses |
| Negation | `"!192.168.1.0/24"` | any address except this subnet |
| Negated list | `"!192.168.1.0/24,8.8.8.8"` | any address except the listed ones |
| Single port | `"443"` | port 443 |
| Port list | `"80,443"` | port 80 or 443 |
| Port range | `"8000-9000"` | ports 8000 through 9000 |
| Negated port | `"!443"` | all ports except 443 |
| Negated port list | `"!80,443"` | all ports except 80 and 443 |

A single `!` at the start negates the entire value. Negation applies to all comma-separated entries.
{{% /details %}}
