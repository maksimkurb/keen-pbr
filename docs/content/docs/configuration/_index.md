---
title: Configuration
weight: 3
---

keen-pbr reads its settings from one JSON config file.

Common config file locations:

- Keenetic / NetCraze: `/opt/etc/keen-pbr/config.json`
- OpenWrt: `/etc/keen-pbr/config.json`
- Debian: `/etc/keen-pbr/config.json`

If you installed the full package, you can usually do your first setup in the Web UI and come back to this section later. Most users only need these four parts of the config:

- [Outbounds](outbounds/) — where matching traffic should go
- [Lists](lists/) — the sites or IP ranges you want to match
- [Route Rules](route-rules/) — which lists go through which outbound
- [DNS](dns/) — which DNS server should be used for those lists

## Practical Example

This example routes `google.com` through `vpn` and leaves everything else on `wan`:

```json
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
  ],
  "lists": {
    "my_sites": {
      "domains": ["google.com"]
    }
  },
  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset",
      "address": "127.0.0.1"
    },
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1",
        "detour": "vpn"
      },
      {
        "tag": "default_dns",
        "address": "1.1.1.1"
      }
    ],
    "rules": [
      {
        "list": ["my_sites"],
        "server": "vpn_dns"
      }
    ],
    "fallback": ["default_dns"]
  },
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

## Basic Configuration

- [Outbounds](outbounds/) — choose the VPN and normal internet connections
- [Lists](lists/) — define the sites, domains, or IP ranges to match
- [Route Rules](route-rules/) — connect each list to an outbound
- [DNS](dns/) — make sure matching domains are resolved through the right DNS server

## Advanced Configuration

These settings are optional for most users:

- [Advanced](advanced/) — API, service paths, automatic list refresh, and low-level routing options

{{< callout type="info" >}}
List names must be 1-24 characters, use only `a-z`, `A-Z`, `0-9`, and `_`, and start with a letter. For consistency, use the same underscore style for outbound and DNS tags too.
{{< /callout >}}
