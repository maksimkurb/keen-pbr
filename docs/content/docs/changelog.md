---
title: Changelog
weight: 8
---

What changed in each keen-pbr release, and what to do when you upgrade.

## 3.4.0 {#v3-4-0}

### What's new

- **Domain lists work without a resolver setup.** keen-pbr reads DNS replies passing through the router and the site names in TLS, HTTP and QUIC connections, and fills the sets of your lists itself. The first connection to a site already takes the right route. See [DNS]({{< relref "/docs/configuration/dns#dns-interception" >}}).
- **dnsmasq is optional.** It is needed only for separate DNS servers for some lists or for your own default DNS servers. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}). keen-pbr can pick the dnsmasq instance, shows its state in the Web UI and repairs it when it loses the config. New commands: `keen-pbr dnsmasq detect` and `keen-pbr dnsmasq remove`.
- **Load balancing across several links** (`strategy: "balance"` in a `urltest` outbound) with weights. It is not available in the Keenetic build. See [Multi-WAN]({{< relref "/docs/configuration/multi-wan" >}}).
- **Automatic gateway detection** for interface outbounds, and editing the default gateway in the Web UI.
- **New Web UI**: compact overview, live table of current requests, request log with explained badges, a diagnostics panel (kernel checks first), updates over a live stream instead of polling, better mobile layout, and a settings page that shows the daemon's defaults.
- **Health checks** warn about missing NAT or strict `rp_filter` on balanced links, foreign fwmarks that overlap `fwmark.mask`, and missing iptables extensions.
- **Prometheus endpoint** with probe latency, balance and interception counters.
- **`keen-pbr test-routing`** takes packet criteria (protocol, ports, addresses, DSCP) and runs in the daemon.
- **The default config** ships a rule that keeps local networks out of tunnels.
- Lists that failed to download at start are retried in the background.
- Removing the package also removes leftover firewall rules and dnsmasq files.

### Upgrading from 3.3.x {#upgrading-from-33x}

After the upgrade, dnsmasq management stays on, as in 3.3.x. If you do not need it, turn off **Manage dnsmasq configuration** in the Web UI (**General** > **Local DNS server management**) and follow the rollback steps: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq-rollback" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq-rollback" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq-rollback" >}}). Without it, DNS rules and default DNS servers stop working.

Check after the upgrade:

- **Keenetic: kernel modules.** Domains are now detected by interception. Install the **Netfilter Subsystem Kernel Modules** component if you have not. Then open the status page in the Web UI and run the DNS check.
- **Keenetic: dnsmasq.** The old keen-pbr block is removed from `/opt/etc/dnsmasq.conf`; the original file is saved as `/opt/etc/dnsmasq.conf.keen-pbr.bak`. If dnsmasq had no DNS server left, the package adds a `# BEGIN keen-pbr fallback upstream` block with public DNS servers. If you turn dnsmasq management off, replace it with your own servers.
- **Changed defaults.**
  - `daemon.clear_dynamic_sets_on_apply` is now `false`: domain addresses survive a config apply.
  - A list's `ttl_ms` is now a lower bound; the upper bound is `intercept.max_ttl_ms`.
  - `iproute.process_router_traffic` is `false`: route rules apply to devices in your network, not to the router's own traffic.
- **Ignored fields.** `dns.system_resolver`, `dns.dns_test_server` (use `intercept.dns.marker`) and `daemon.resolver_ready_timeout_seconds` no longer do anything; the log warns if they are set. The old `outbounds`, `candidates` and `weight` fields in `outbound_groups` are replaced by `members`; old configs keep working.
- **New field `version`.** It records which keen-pbr version the config was written for. keen-pbr sets it when you save from the Web UI. Keep it if you edit the file by hand.
