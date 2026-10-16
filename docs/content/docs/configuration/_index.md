---
title: Configuration
weight: 3
---

keen-pbr reads its settings from one JSON config file.

Common config file locations:

- Keenetic / NetCraze: `/opt/etc/keen-pbr/config.json`
- OpenWrt: `/etc/keen-pbr/config.json`
- Debian: `/etc/keen-pbr/config.json`

If you installed the full package, configure keen-pbr in the Web UI: it writes the config for you, so you do not need to edit the file. If you installed `keen-pbr-headless` (no Web UI), edit the config file. Configuration has four parts:

- [Outbounds]({{< relref "/docs/configuration/outbounds" >}}) — the channels traffic can take: a VPN, your ISP connection, or a block
- [Lists]({{< relref "/docs/configuration/lists" >}}) — the sites and IP ranges you want to route
- [Route Rules]({{< relref "/docs/configuration/route-rules" >}}) — which list goes through which outbound
- [DNS]({{< relref "/docs/configuration/dns" >}}) — DNS servers and DNS interception

In the Web UI they correspond to the **Routing** > **Outbounds**, **Routing** > **Lists**, **Routing** > **Routing rules** and **General** > **Settings** menu items.

## Practical Example

This example sends `google.com` through the VPN (`tun0`); all other traffic goes the usual way, through the router's main connection:

```json
{
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0"
    }
  ],
  "lists": {
    "my_sites": {
      "domains": ["google.com"]
    }
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

## Advanced Configuration

These settings are optional for most users:

- [Advanced]({{< relref "/docs/configuration/advanced" >}}) — API, service paths, automatic list refresh, and low-level routing options
- [Full Reference Config]({{< relref "/docs/configuration/full-reference-config" >}}) — commented config example with every supported config option

{{< callout type="info" >}}
List names, outbound tags, and DNS server tags must all follow the same convention: `^[a-z][a-z0-9_]*$` and must be at most 24 characters.
{{< /callout >}}
