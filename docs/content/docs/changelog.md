---
title: Changelog
weight: 8
---

What changed in each keen-pbr release, and what to do when you upgrade.

## 3.4.0 {#v3-4-0}

### What's new

- **Domain lists work without a resolver setup.** keen-pbr reads DNS replies passing through the router and the site names in TLS, HTTP and QUIC connections, and fills the sets of your lists itself. The first connection to a site already takes the right route. If the kernel lacks a feature, only that part (DNS or connections) is turned off, and the status page says why. See [DNS]({{< relref "/docs/configuration/dns#dns-interception" >}}).
- **dnsmasq is optional.** It is needed only for separate DNS servers for some lists or for your own default DNS servers. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}). keen-pbr can pick the dnsmasq instance, shows its state in the Web UI and repairs it when it loses the config. New commands: `keen-pbr dnsmasq detect` and `keen-pbr dnsmasq remove`.
- **Load balancing across several links** (`strategy: "balance"` in a `urltest` outbound) with a weight for each link. A connection stays on the link it started on. It is not available in the Keenetic build. See [Multi-WAN Load Balancing]({{< relref "/docs/configuration/multi-wan" >}}).
- **Automatic gateway detection** for interface outbounds (`"gateway": "auto"`), and editing the default gateway in the Web UI. See [interface]({{< relref "/docs/configuration/outbounds#interface" >}}).
- **All internet traffic through failover or balancing**: the new `default_gateway` route rule condition (nftables only). See [Route rules]({{< relref "/docs/configuration/route-rules#failover-or-balancing-for-all-internet-traffic" >}}).
- **New Web UI**: compact overview, live table of current requests, request log with explained badges and filters, a diagnostics panel (kernel checks first), redesigned outbound editor, updates over a live stream instead of polling, better mobile layout, and a settings page that shows the daemon's defaults.
- **Health checks** warn about missing NAT or strict `rp_filter` on balanced links, foreign fwmarks that overlap `fwmark.mask`, and missing iptables extensions. `keen-pbr status` shows the same issue list as the Web UI.
- **Prometheus endpoint** `GET /metrics` with probe latency, link selection, list updates, balance and interception counters, plus a Grafana dashboard and alert examples. See [REST API]({{< relref "/docs/rest-api#get-metrics" >}}).
- **`keen-pbr test-routing`** takes packet criteria (protocol, ports, addresses, DSCP) and runs in the daemon, the same way as the routing test in the Web UI.
- **The router's own traffic is left alone by default.** Route rules apply to devices in your network. To include the router, set `iproute.process_router_traffic` to `true`. See [iproute]({{< relref "/docs/configuration/advanced#iproute" >}}).
- **The default config** ships a rule that keeps local networks out of tunnels.
- **The "What's new?" button** in the Web UI opens this page.
- Lists that failed to download at start are retried in the background.
- Removing the package also removes leftover firewall rules and dnsmasq files.

### Fixes

- `urltest` and `icmptest` groups accepted members of any type, although only `interface` and `table` work. Other types now fail config validation. A `blackhole` member was never selected: keen-pbr skips it and logs a warning. You do not need it to block traffic: when no link works, traffic is blocked anyway.
- Automatic list updates stopped after the first list change that affected routing. They now keep running on schedule.
- New devices could not get an address over DHCP when a route rule matched all UDP traffic.
- With `inbound_interfaces` set, the router's own traffic skipped route rules even when it should have matched.
- When several DNS rules named the same list, only the first rule's server was used. Now dnsmasq gets the servers of every rule and switches between them.
- On Keenetic, a burst of route changes on WAN DHCP renewal could leave routing built from a half-updated table.
- After a power loss a list cache file could be left empty.
- `keen-pbr status` reported every `urltest` and `icmptest` rule as an error.
- The Web UI dropped `conntrack_on_switch`, `strict_enforcement_action` and the `urltest` probe timeout when saving an outbound.

### Upgrading from 3.3.x {#upgrading-from-33x}

After the upgrade, dnsmasq management stays on, as in 3.3.x. If you do not need it, turn off **Manage dnsmasq configuration** in the Web UI (**General** > **Settings** > **Local DNS server management**) and follow the rollback steps: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq-rollback" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq-rollback" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq-rollback" >}}). Without it, DNS rules and default DNS servers stop working.

**Keenetic.** keen-pbr 3.3.x installed `dnsmasq-full` as a dependency, and opkg removes it during the upgrade. When it is removed, the installer asks in the terminal what to do:

- `1`: keep dnsmasq. Right after opkg finishes, `dnsmasq-full` is installed again, with its progress printed in the same terminal.
- `2`: stop using dnsmasq. dnsmasq management is turned off.

If you installed `dnsmasq-full` yourself, opkg keeps it and there is no question. If the install fails, install the package by hand, record the choice with `keen-pbr dnsmasq finish-upgrade keep` and restart the service: `/opt/etc/init.d/S80keen-pbr restart`.

Check after the upgrade:

- **Keenetic: kernel modules.** Domains are now detected by interception. Install the **Netfilter Subsystem Kernel Modules** component if you have not. Then open the status page in the Web UI and run the DNS check.
- **Keenetic: iptables only.** The Keenetic build always uses iptables. A config with `daemon.firewall_backend: "nftables"` or `strategy: "balance"` does not pass validation there.
- **Debian 11 (bullseye)** is no longer supported.
- **Changed defaults.**
  - `daemon.clear_dynamic_sets_on_apply` is now `false`: domain addresses survive a config apply.
  - A list's `ttl_ms` is now a lower bound; the upper bound is `intercept.max_ttl_ms`.
  - `iproute.process_router_traffic` is `false`: route rules apply to devices in your network, not to the router's own traffic.
  - `--log-target` is `both` for every command. The packaged service files pass `--log-target syslog`.
- **Ignored fields.** `dns.system_resolver`, `dns.dns_test_server` (use `intercept.dns.marker`) and `daemon.resolver_ready_timeout_seconds` no longer do anything; the log warns if they are set. The old `outbounds`, `candidates` and `weight` fields in `outbound_groups` are replaced by `members`; old configs keep working, and keen-pbr rewrites them once, keeping a copy in `<config>.bak-pre-members`.
- **New field `version`.** It records which keen-pbr version the config was written for. keen-pbr sets it when you save from the Web UI. Keep it if you edit the file by hand.
- **DNS check domain.** To check DNS by hand, run `nslookup check.keen-pbr.fyi` instead of `nslookup check.keen.pbr`. See [Troubleshooting]({{< relref "/docs/troubleshooting" >}}).

If you use scripts or other programs with keen-pbr:

- `keen-pbr resolver-config-hash` does nothing and always succeeds.
- `keen-pbr generate-resolver-config` takes `dnsmasq` instead of `dnsmasq-ipset` and `dnsmasq-nftset`.
- `keen-pbr status` exits with code 1 when any check failed or any issue is an error.
- `GET /api/dns/test?show=all` (and `show=full`) works only with API authentication turned on.
- New endpoints: `GET /metrics`, `GET /api/health/issues`, `GET /api/config/defaults`. `GET /api/status/events` also sends issues, routing health, authentication state and config changes.
