# Changelog

What changed in each keen-pbr release, and what to do when you upgrade.

## 3.4.0 {#v3-4-0}

### What's new

- **Domain lists work without a resolver setup.**\
  keen-pbr reads DNS replies passing through the router and the site names in TLS, HTTP and QUIC connections, and fills the sets of your lists itself. If a device looks up the site over plain DNS, the very first connection already takes the right route. If the device uses encrypted DNS, keen-pbr learns the site only from its name at the start of the connection. That first connection breaks, and the browser or app reconnects, this time over the right route. If the kernel lacks a feature, only that part (DNS or connections) is turned off, and the status page says why. See [DNS]({{< relref "/docs/configuration/dns#dns-interception" >}}).
- **dnsmasq is optional.**\
  You need it only for {{< term "split DNS" >}} (different DNS servers depending on the domain) or to set DNS servers for the whole network right in keen-pbr. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}). keen-pbr can pick the dnsmasq instance, shows its state in the Web UI and repairs it when it loses the config. New command: `keen-pbr resolver dnsmasq detect`.
- **Load balancing across several links.** {{< only openwrt debian >}}\
  A `urltest` or `icmptest` outbound with `strategy: "balance"` spreads connections across links, each with its own weight. A connection stays on the link it started on. See [Multi-WAN Load Balancing]({{< relref "/docs/configuration/multi-wan" >}}).
- **Automatic gateway detection.**\
  An `interface` outbound can use `"gateway": "auto"`, and the default gateway can now be edited in the Web UI. See [interface]({{< relref "/docs/configuration/outbounds#interface" >}}).
- **All internet traffic through failover or balancing.** {{< only nftables >}}\
  The new `default_gateway` route rule condition. See [Route rules]({{< relref "/docs/configuration/route-rules#failover-or-balancing-for-all-internet-traffic" >}}).
- **New Web UI.**\
  Compact overview, live table of current requests, request log with explained badges and filters, a diagnostics panel (kernel checks first), redesigned outbound editor, updates over a live stream instead of polling, better mobile layout, and a settings page that shows the daemon's defaults.
- **Health checks.**\
  keen-pbr warns about missing NAT or strict `rp_filter` on balanced links, foreign fwmarks that overlap `fwmark.mask`, and missing iptables extensions. `keen-pbr status` shows the same issue list as the Web UI.
- **Prometheus endpoint.**\
  `GET /metrics` reports probe latency, link selection, list updates, balance and interception counters. A Grafana dashboard and alert examples are included. See [REST API]({{< relref "/docs/rest-api#get-metrics" >}}).
- **Packet criteria in `keen-pbr test-routing`.**\
  The command takes protocol, ports, addresses and DSCP, and runs in the daemon, the same way as the routing test in the Web UI.
- **The router's own traffic is left alone by default.**\
  Route rules apply to devices in your network. To include the router, set `iproute.process_router_traffic` to `true`. See [iproute]({{< relref "/docs/configuration/advanced#iproute" >}}).
- **Local networks rule in the default config.**\
  It keeps local networks out of tunnels.
- **The "What's new?" button.**\
  It opens this page from the Web UI.
- Lists that failed to download at start are retried in the background.
- Removing the package also removes leftover firewall rules and dnsmasq files.

### Fixes

- `urltest` and `icmptest` groups now accept only `interface` and `table` members: other types never worked and now fail config validation. A `blackhole` member was never selected; keen-pbr now skips it and logs a warning. You do not need it to block traffic: when no link works, traffic is blocked anyway.
- Fixed automatic list updates stopping after the first list change that affected routing. Lists now update on schedule.
- Fixed new devices not getting an address over DHCP when a route rule matches all UDP traffic.
- Fixed the router's own traffic skipping route rules when `inbound_interfaces` is set.
- When several DNS rules name the same list, dnsmasq now gets the servers of every rule and switches between them. Before, only the first rule's server was used.
- {{< only keenetic >}} Fixed routing being built from a half-updated route table when routes change one after another during a WAN DHCP renewal.
- Fixed a list cache file being left empty after a power loss.
- Fixed `keen-pbr status` reporting every `urltest` and `icmptest` rule as an error.
- The Web UI now keeps `conntrack_on_switch`, `strict_enforcement_action` and the `urltest` probe timeout when saving an outbound.
- Fixed DNS servers with `detour` through a tunnel: queries entered the tunnel with the ISP address and got no answer. They now use the tunnel address (for servers with an IPv4 address).

### Breaking Changes {#upgrading-from-33x}

{{< only debian >}} **Before the upgrade.** keen-pbr 3.3.x depended on `dnsmasq`, so apt marked it as automatically installed, and `apt autoremove` may remove it. If you need dnsmasq, first run:

```bash {filename="bash"}
sudo apt-mark manual dnsmasq
```

{{< only openwrt keenetic >}} **Before the upgrade.** keen-pbr 3.3.x installed `dnsmasq-full` as a dependency, and opkg may remove it during the upgrade. If you need dnsmasq, first run over SSH:

```bash {filename="bash"}
opkg flag user dnsmasq-full
```

{{< only keenetic >}} If the package is already gone and you turned on `opkg dns-override`, devices in your network are left without DNS: the router still hands their queries to dnsmasq, which is gone. The keen-pbr Web UI offers two ways out:

- **Keep dnsmasq.**\
  Install it again with `opkg install dnsmasq-full`.
- **Go without dnsmasq.**\
  Turn off **Manage dnsmasq configuration**. Then, in the router's web console (http://my.keenetic.net/a), run `no opkg dns-override` and `system configuration save`, and reboot the router.

After the upgrade, dnsmasq management stays on, as in 3.3.x. If you do not need it, turn off **Manage dnsmasq configuration** in the Web UI (**General** > **Settings** > **Local DNS server management**) and follow the rollback steps: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq-rollback" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq-rollback" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq-rollback" >}}). Without it, DNS rules and default DNS servers stop working.

- First release. Follow-up builds fixed `download` messages for local lists, the default dnsmasq config, and the installer, which no longer replaces an existing `dnsmasq.conf`.

