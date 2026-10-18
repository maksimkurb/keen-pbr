---
title: Changelog
weight: 8
---

What changed in each keen-pbr release, and what to do when you upgrade.

## 3.4.0 {#v3-4-0}

### What's new

- **Domain lists work without a resolver setup.**\
  keen-pbr reads DNS replies passing through the router and the site names in TLS, HTTP and QUIC connections, and fills the sets of your lists itself. If a device looks up the site over plain DNS, the very first connection already takes the right route. If the device uses encrypted DNS, keen-pbr learns the site only from its name at the start of the connection. That first connection breaks, and the browser or app reconnects, this time over the right route. If the kernel lacks a feature, only that part (DNS or connections) is turned off, and the status page says why. See [DNS]({{< relref "/docs/configuration/dns#dns-interception" >}}).
- **dnsmasq is optional.**\
  You need it only for {{< term "split DNS" >}} (different DNS servers depending on the domain) or to set DNS servers for the whole network right in keen-pbr. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}). keen-pbr can pick the dnsmasq instance, shows its state in the Web UI and repairs it when it loses the config. New commands: `keen-pbr resolver dnsmasq detect` and `keen-pbr resolver dnsmasq print-config`.
- **DNS-over-HTTPS.**\
  Added DNS-over-HTTPS support: add a DNS server of type **DNS-over-HTTPS** and use it in DNS rules and among the main DNS servers. See [DNS]({{< relref "/docs/configuration/dns#dns-over-https" >}}).
- **Bootstrap servers for DoH.**\
  You can choose which plain servers keen-pbr asks for the addresses of DoH servers. A DoH server can have its IP addresses set. See [DNS]({{< relref "/docs/configuration/dns#doh-bootstrap" >}}).
- **Several DNS servers for a list.**\
  Added the ability to pick several DNS servers for a list. Requests go to them in turn, and if a server does not answer, the next one takes over. In `config.json` the `server` field of a DNS rule is now `servers`, an array of server names; keen-pbr converts old configs automatically. dnsmasq sends queries for domains of lists with several servers or with DNS-over-HTTPS to the keen-pbr built-in DNS proxy on `127.0.0.89`, with ports starting at 52100. You can change the address and the first port in the **Advanced** settings mode: **DNS proxy address** and **First DNS proxy port**. See [DNS]({{< relref "/docs/configuration/dns#per-list-dns-servers-dnsmasq" >}}).
- The main DNS servers moved from the **DNS rules** page to the **DNS servers** page. A new DNS rule no longer starts with a server picked: choose one yourself.
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
- **Checking the DNS servers.**\
  The **Check** button on the **DNS servers** page asks all servers about the sites you choose and shows which do not answer and which tamper with answers: they say a site does not exist, answer with a stub address, or answer with addresses for names that do not exist. See [DNS]({{< relref "/docs/configuration/dns#check-dns-servers" >}}).
- **Warning about a DNS server that does not answer.**\
  If 10 requests in a row to a server got no answer or ended with an error, the status page shows a warning. See [Troubleshooting]({{< relref "/docs/troubleshooting#dns-server-not-answering" >}}).
- **DNS proxy metrics.**\
  `/metrics` shows the queries to the DNS proxy, the answers of the servers by result, the answer time and the servers the proxy is skipping now. The Grafana dashboard has a new DNS proxy section. See [REST API]({{< relref "/docs/rest-api#dns-proxy" >}}).
- **Packet criteria in `keen-pbr test-routing`.**\
  The command takes protocol, ports, addresses and DSCP, and runs in the daemon, the same way as the routing test in the Web UI.
- **The router's own traffic is left alone by default.**\
  Route rules apply to devices in your network. To include the router, set `iproute.process_router_traffic` to `true`. See [iproute]({{< relref "/docs/configuration/advanced#iproute" >}}).
- **Local networks rule in the default config.**\
  It keeps local networks out of tunnels.
- **The "What's new?" button.**\
  It opens this page from the Web UI.
- New lists are downloaded in the background after a configuration is applied, not before. A failed download no longer cancels the apply and shows up in the warnings instead. Lists that failed to download at start are retried the same way.
- Removing the package also removes leftover firewall rules and dnsmasq files.

### Fixes

- Fixed applying a configuration where a new list downloads through a new outbound: the apply used to fail with "Failed to prepare remote list(s)".
- `urltest` and `icmptest` groups now accept only `interface` and `table` members: other types never worked and now fail config validation. A `blackhole` member was never selected; keen-pbr now skips it and logs a warning. You do not need it to block traffic: when no link works, traffic is blocked anyway.
- Fixed automatic list updates stopping after the first list change that affected routing. Lists now update on schedule.
- Fixed new devices not getting an address over DHCP when a route rule matches all UDP traffic.
- Fixed the router's own traffic skipping route rules when `inbound_interfaces` is set.
- When several DNS rules name the same list, the list now uses the servers of every rule. Before, only the first rule's server was used.
- {{< only keenetic >}} Fixed routing being built from a half-updated route table when routes change one after another during a WAN DHCP renewal.
- Fixed a list cache file being left empty after a power loss.
- {{< only keenetic >}} Fixed a package upgrade where the old version did not stop in time: it kept running after the install instead of the new one.
- Fixed the routing test with IPv6 turned off: it used to check the domain's IPv6 addresses too.
- Fixed the routing test showing one route rule as several blocks.
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

After the upgrade, dnsmasq management stays on, as in 3.3.x. If you do not need it, turn off **Manage dnsmasq configuration** in the Web UI (**General** > **Settings** > **Local DNS server management**) and follow the rollback steps: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq-rollback" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq-rollback" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq-rollback" >}}). Without it, DNS rules and main DNS servers stop working.

What to check after the upgrade:

- **Kernel modules.** {{< only keenetic >}}\
  Domains are now detected by interception. If the **Netfilter Subsystem Kernel Modules** and **Xtables-addons Extension Package for Netfilter** components are not installed yet, install them. Then open the status page in the Web UI and run the DNS check.
- **iptables only.** {{< only keenetic >}}\
  The Keenetic build always works through iptables. A config with `daemon.firewall_backend: "nftables"` or `strategy: "balance"` fails validation there.
- **Debian 11 (bullseye) is no longer supported.**
- **A list's `ttl_ms` is now a minimum time.**\
  It used to set how long the addresses of a list's domains are kept. Now it is a minimum: if the {{< term "TTL" >}} in the DNS reply is longer, the address is kept longer, but no more than `intercept.max_ttl_ms`. If `ttl_ms` is not set, the minimum comes from `intercept.min_ttl_ms` (5 minutes by default).
- **Defaults changed.**
  - `daemon.clear_dynamic_sets_on_apply` is now `false`: domain addresses are kept when a config is applied.
  - `iproute.process_router_traffic` is `false`: route rules apply to devices in your network, not to the router's own traffic.
  - `--log-target` is `both` for all commands. The service package files pass `--log-target syslog`.
- **Fields that no longer work.**\
  `dns.system_resolver`, `dns.dns_test_server` (use `intercept.dns.marker`) and `daemon.resolver_ready_timeout_seconds`; the log warns if they are set. The old `outbounds`, `candidates` and `weight` fields in `outbound_groups` are replaced by `members`. Old configs keep working: keen-pbr rewrites them to the new format once (comments and key order are not kept).
- **New `version` field.**\
  It holds the keen-pbr version the config was written for. keen-pbr sets it when you save the config in the Web UI. If you edit the file by hand, leave it in place.
- **DNS check domain.**\
  For a manual check, run `nslookup check.keen-pbr.fyi` instead of `nslookup check.keen.pbr`. See [Troubleshooting]({{< relref "/docs/troubleshooting" >}}).

If your scripts or other programs work with keen-pbr:

- The `keen-pbr resolver-config-hash` command does nothing and always succeeds.
- `keen-pbr generate-resolver-config` accepts `dnsmasq` instead of `dnsmasq-ipset` and `dnsmasq-nftset`.
- `keen-pbr status` exits with code 1 if any check failed or the problems include an error.
- `GET /api/dns/test?show=all` (and `show=full`) works only when API authentication is enabled.
- New endpoints: `GET /metrics`, `GET /api/health/issues`, `GET /api/config/defaults`. `GET /api/status/events` also sends problems, routing state, authentication state and config changes.

## 3.3.1 {#v3-3-1}

_25 August 2026_

### What's new

- **Where the log goes.**\
  The `--log-target stderr|syslog|both` option. The service writes to syslog by default, commands in a terminal write to stderr.

### Fixes

- **Switching open connections.**\
  When the selected link in a `urltest` or `icmptest` group stops working, keen-pbr now clears the {{< term "conntrack" >}} entries of open connections, and devices reopen them through the new link, over IPv4 and IPv6. Before, connections stayed on the dead link. If the old link still works at the time of the switch, the `conntrack_on_switch` setting applies.
- Fixed the display of member states in `urltest` and `icmptest` groups (primary, backup, flaky, unavailable).
- keen-pbr waits for dnsmasq to be ready for as long as `daemon.resolver_ready_timeout_seconds` says, not always five seconds.

## 3.3.0 {#v3-3-0}

_21 August 2026_

### What's new

- **Large IP lists.**\
  You can raise the {{< term "ipset" >}} size with `ipset_hashsize` and `ipset_maxelem`: **General** > **Settings** > **Advanced routing settings**.
- On `SIGUSR1` keen-pbr no longer reloads the address lists. `daemon.reuse_static_sets_on_runtime_refresh: false` brings the old behavior back.
- **Config rollback.**\
  If a config fails to apply, you can go back to the previous one.
- The Web UI reconnects by itself when the connection to the router is lost.
- **RAW separately for IPv4 and IPv6.** {{< only keenetic >}}\
  `KEEN_PBR_RAW_PREROUTING` values: `auto`, `enable`, `disable`, `ipv4-only` and `ipv6-only`. See [RAW or mangle]({{< relref "/docs/getting-started/installation/keenetic#raw-or-mangle" >}}).

### Fixes

- **CPU load at idle.**\
  Fixed: keen-pbr polled state all the time and used about 20% of the CPU (measured on a mipsel router). The state is now updated on events.
- Removed one cause of the `Fatal error: control socket timeout waiting for socket ack` error, which kept a large config from applying and dnsmasq from starting on slow routers.

### Breaking Changes {#upgrading-from-32x}

- **The `enable` value for RAW.** {{< only keenetic >}}\
  `KEEN_PBR_RAW_PREROUTING=enable` now requires RAW support for both IPv4 and IPv6. If you need RAW for only one of them, use `ipv4-only` or `ipv6-only`.

## 3.2.1 {#v3-2-1}

_15 August 2026. Version 3.2.0 was never released._

### What's new

- **Password for the Web UI and API.**\
  Password login, Basic Auth for scripts, and a list of sites the browser may let call the API (CORS). All of it is on the new **Security** page. See [REST API]({{< relref "/docs/rest-api" >}}).
- **The `icmptest` outbound.**\
  Checks links with a ping ({{< term "ICMP" >}}) instead of loading a URL. See [icmptest]({{< relref "/docs/configuration/outbounds#icmptest" >}}).
- You can give the router a name, and it is shown in the Web UI.
- The route check in the Web UI was redone. `keen-pbr test-routing` prints plain text instead of JSON.
- Unapplied config changes can be discarded in the Web UI and through the API.
- The Web UI now has links to the documentation.
- You can change the order of route rules by dragging.
- **RAW or mangle.** {{< only keenetic >}}\
  In `/opt/etc/keen-pbr/defaults` you can pick the table in which devices' IPv4 traffic is marked. RAW is not flushed when Keenetic rebuilds its firewall and connections do not break, but it works only for devices with the **Default policy**. {{< term "mangle" >}} suits any policy from **Connection priorities**, but the rules disappear while the firewall is being rebuilt. See [RAW or mangle]({{< relref "/docs/getting-started/installation/keenetic#raw-or-mangle" >}}).
- Packages for Debian 11 (Bullseye), 12 (Bookworm) and 13 (Trixie).
- The full package got smaller.
- The package repository moved from GitHub Pages to a separate paid host, so it is down less often.

### Fixes

- **Start after a router reboot.** {{< only keenetic >}}\
  Fixed: keen-pbr started before {{< term "RCI" >}} and might fail to start. The service now waits for RCI. [Details](https://t.me/keen_pbr/1/3041)
- {{< only keenetic >}} Fixed rules with a {{< term "DSCP" >}} condition: the `xt_dscp` module is now loaded automatically.
- Fixed a resource leak in the Web UI update stream.

## 3.1.1 {#v3-1-1}

_26 July 2026_

### What's new

- The diagnostics report you download from the Web UI now includes the last system command that failed.

### Fixes

- {{< only iptables >}} Fixed applying iptables rules, which sometimes did not work.

## 3.1.0 {#v3-1-0}

_25 July 2026_

### What's new

- **Safe config apply.**\
  keen-pbr first checks the new config and applies it, waits for dnsmasq to confirm, and only then saves it to disk. If something goes wrong, the file on disk stays as it was.
- **Apply without gaps.**\
  nftables changes in a single transaction, and iptables and ipset are replaced without clearing them first, so traffic does not bypass the rules while a config is applied. keen-pbr removes only its own routes and rules and leaves others alone.
- **Strict mode.**\
  If no link works, traffic is blocked with an `unreachable` or `blackhole` route.
- The router's own DNS queries go through `detour` too.
- **The `dscp` condition in route rules.**\
  Route by the {{< term "DSCP" >}} mark (values 1-63) that a program on a device sets. See the [DSCP example]({{< relref "/docs/configuration/route-rules#dscp-filter--route-tagged-traffic-through-vpn" >}}).
- **The `conntrack_on_switch` setting.**\
  Sets what `urltest` does with open connections when the link changes: `preserve` or `delete`.
- **Strict list checks.**\
  URL lists are downloaded only over HTTP and HTTPS, redirects included. A local list must be a regular file no larger than the set size. Invalid domains are dropped. If the same list is downloaded several times at once, only one download runs.
- **The Web UI updates itself.**\
  Changes in service, outbound and interface state show up right away, with no page refresh. Diagnostics and the DNS check cannot do this yet.
- On start, stop, restart and config apply you can see which stage the service is in.
- **Detailed diagnostics.**\
  You can see whether the config in memory matches the config on disk, whether dnsmasq got the current config, and which cache files are missing.
- `keen-pbr status` takes its data from the running service.
- **Marks in the RAW table.** {{< only keenetic >}}\
  Devices' IPv4 traffic is marked in the RAW table. Keenetic does not rebuild it, so games and calls drop less often. If RAW is not available, keen-pbr warns you and uses {{< term "mangle" >}}.
- {{< only keenetic >}} Next to the system interface names, the Web UI shows their names from Keenetic.
- The package now depends on `conntrack`.
- In the API, the `apply`, `start`, `stop` and `restart` commands reply with `202` and an operation number. If such a command is already running, the next one gets `409`.

### Fixes

- The route check in the Web UI is faster: up to eight addresses are checked at once.
- Fixed the whole rule being deleted when you removed one list from a rule with several conditions.

## 3.0.7 (beta) {#v3-0-7}

_29 June 2026_

### Fixes

- {{< only keenetic >}} Fixed the IPv6 support check in iptables mode: keen-pbr could decide that IPv6 is not supported ([#139](https://github.com/maksimkurb/keen-pbr/issues/139)).

## 3.0.6 (beta) {#v3-0-6}

_3 June 2026. Version 3.0.5 was never released: it had a bug that kept keen-pbr from installing on OpenWrt 25.x._

### Fixes

- Fixed problems that `keen-pbr status` reported while everything was fine in the Web UI.
- A `urltest` link selected for both IPv4 and IPv6 is no longer treated as an error.
- `keen-pbr status` marks rules that pass traffic through unchanged as `PASS`.
- With IPv6 turned off, `keen-pbr status` no longer shows IPv6 sets.
- {{< only openwrt >}} Fixed the `.apk` packages and the repository index for OpenWrt 25.x.
- Fixed IPv6 detection with old versions of `ip6tables-restore` ([#138](https://github.com/maksimkurb/keen-pbr/issues/138)).

## 3.0.4 (beta) {#v3-0-4}

_27 May 2026_

### What's new

- **The `daemon.ipv6_enabled` setting.**\
  For networks without IPv6: with `false`, keen-pbr creates no IPv6 rules. If IPv6 is on but the system does not support it, keen-pbr says so in the log and works with IPv4 only.
- **Config reference check.**\
  Before applying, keen-pbr finds references that lead nowhere: a rule pointing to a missing list or outbound, a DNS rule pointing to a missing list or server, an `interface` outbound without an interface, a route check without an address.
- dnsmasq state is checked every 5 seconds instead of 30. When lists or DNS change, keen-pbr reloads dnsmasq and checks the result right away.
- **Bulk actions.**\
  In the Web UI tables you can select several rows and act on all of them at once.
- **What changes on delete.**\
  Before deleting, you can see which rules will be removed and which DNS servers and `urltest` groups will change.
- **Interface picker.**\
  Shows all known interfaces, whether they are up and their addresses. You can search by address too.
- **Detailed route check.**\
  Shows which rule matched, which outbound and interface were chosen, and which lists the address is in.
- Before you change or delete a list, you can see where else it is used.
- Config validation errors are shown next to the fields when you save.
- {{< only keenetic >}} On start, kernel ipset support is checked. If Netfilter components are missing, the error message is clearer. Messages about loading the `xt_multiport` module are clearer too.

### Fixes

- A URL list with inline entries no longer loses them when it is loaded from the cache.
- Fixed the Web UI on iPhone and in Safari on macOS.
- Fixed interface tracking: after a network error keen-pbr stopped noticing interface changes.
- The reload and stop commands find the service by its PID file and no longer touch other running keen-pbr processes.
- Fixed simultaneous DNS checks that interfered with each other.

## 3.0.3 (beta) {#v3-0-3}

_10 May 2026_

### What's new

- **The `gateway6` field.**\
  Sets the IPv6 gateway of an `interface` outbound.
- **List update errors.**\
  Shown in the log, through the API and on the lists page.
- **List auto-update once a week.**\
  This is the default in new configs, it used to be once a day. Existing configs keep their schedule.
- A DNS server tag can no longer be changed when editing.

### Fixes

- **IPv6 leaking past the VPN.**\
  Fixed: the missing IPv6 routes were added, and for `urltest` an `unreachable` route was added for when no link works.
- Fixed checking links over IPv6.
- Fixed keen-pbr crashing when the router has no internet.
- Fixed the handling of the `urltest` check timeout and the priority of `unreachable` routes.
- {{< only nftables >}} Fixed port forwarding when the request comes from an address in a list.
- Fixed the display of list auto-update: the Web UI showed it as on when it was off.
- The external link icon is now clickable.

## 3.0.2 (beta) {#v3-0-2}

_26 April 2026_

### Breaking Changes {#upgrading-from-301}

- **New repository address.** {{< only openwrt >}}\
  Packages are now built per CPU architecture, not per router model. If keen-pbr is already installed on OpenWrt, change the repository address in the package manager settings. See [Installing on OpenWrt]({{< relref "/docs/getting-started/installation/openwrt" >}}).

### Fixes

- Fixed the **Apply and restart** button in the bottom bar, which sometimes did nothing.

## 3.0.1 (beta) {#v3-0-1}

_25 April 2026_

### What's new

- Improved the overview page.

### Fixes

- **The route check no longer touches the firewall.** {{< only nftables >}}\
  Before, the route check in the Web UI wiped firewall rules.
- Fixed dnsmasq starting after a router reboot: sometimes it did not start.
- **Route check through dnsmasq.**\
  Addresses are resolved through dnsmasq, not the ISP's DNS. Before, the check could get a reply forged by the ISP and show a wrong route.
- {{< only openwrt >}} Fixed nftables rules disappearing.
- **fw4 support.** {{< only openwrt >}}\
  The firewall integration was redone: on newer OpenWrt versions the `/etc/hotplug.d/firewall/70-keen-pbr` script stopped running.
- {{< only openwrt >}} `dnsmasq-full` was added to the package dependencies. Fixed writing to the system log.
- {{< only keenetic >}} Fixed starting on old NDMS 2.x firmware with `keenetic`-type DNS.
- {{< only keenetic >}} {{< term "DoT" >}} DNS servers from Keenetic settings are now used.
- {{< only keenetic >}} `iptables` and `ipset` were added to the package dependencies.
- {{< only keenetic >}} Fixed `opkg update` errors for those who still had the old repository address.
- Fixed the **Save** button: after an error in the rule form it could not be clicked until the page was reloaded.
- {{< only iptables >}} The port list is checked against iptables limits.

## 3.0.0 (beta) {#v3-0-0}

_22 April 2026_

keen-pbr was rewritten from scratch. Version 2.x worked only on Keenetic and Netcraze, version 3 also works on OpenWrt and Debian.

- **Web UI and REST API.**\
  The Web UI is easy to use from a phone too.
- **Link checks (`urltest`).**\
  keen-pbr checks by itself whether a link has internet and switches traffic to another one when it fails. It works with any VPN, including one installed through opkg, for example AmneziaWG-Go. Version 2.x learned a link's state only from the Keenetic API.
- **New rule conditions.**\
  Ports and source and destination addresses, exclusions with `!`, rules without lists. For example, you can exclude one TV from routing or send all traffic of one device to a VPN.
- **Inbound interface filter.**\
  For example, rules apply in the main network and leave the guest network alone.
- **Gateway for the `interface` outbound.**\
  You can set it explicitly.
- **Automatic priorities.**\
  Routing tables, {{< term "fwmark" >}} values and `ip rule` priorities are assigned in the order of the rules.
- **DNS rules separate from routing.**\
  You can set a DNS server for domains without changing their route.
- **Temporarily turning rules off.**\
  A rule can be switched off without deleting it.

How to move from 2.x: [Upgrade from 2.x]({{< relref "/docs/getting-started/upgrade-from-2x" >}}).

## keen-pbr 2.x {#v2}

Versions for Keenetic and Netcraze. The config is in TOML format, in the `keen-pbr.conf` file.

### 2.2.1 {#v2-2-1}

_10 June 2025_

- If the Keenetic API returned no data for an interface but the system sees it as up, the interface is treated as working.
- **Own DNS server for ipset.**\
  The `override_dns` option in `[ipset.routing]` sets a separate DNS server for the domains of that ipset.
- If an interface from the config is missing, keen-pbr keeps working.
- `keen-pbr self-check` now finds IPv6 `blackhole` routes correctly.

### 2.1.4 {#v2-1-4}

_11 May 2025_

- The `keen-pbr dns` command shows the DNS servers from the Keenetic profile.
- **DoH and DoT from Keenetic.**\
  The `use_keenetic_dns` option turns on {{< term "DoH" >}} and {{< term "DoT" >}} from the Keenetic settings, and `fallback_dns` sets a DNS server for when the {{< term "RCI" >}} query fails.
- **Default policy only.**\
  The default iptables rule serves only devices with the default policy in **Connection priorities**.

### 2.0.3 {#v2-0-3}

_25 February 2025_

- Fixed the package install and uninstall scripts.

### 2.0.2 {#v2-0-2}

_17 February 2025_

- Fixed the `certificate signed by unknown authority` error that kept lists from downloading on some routers.

### 2.0.1 {#v2-0-1}

_16 February 2025_

- **New name.**\
  The project was renamed from keenetic-pbr to keen-pbr, and the repository address changed. How to upgrade from 1.x is in the [GitHub release notes](https://github.com/maksimkurb/keen-pbr/releases/tag/v2.0.1).
- **No files in dnsmasq.d.**\
  keen-pbr no longer writes files to `/opt/etc/dnsmasq.d/`: dnsmasq gets the lists from keen-pbr at startup. Because of this dnsmasq starts more slowly: about 3.5 seconds for 43 thousand domains on a Keenetic Viva.
- **A separate section for lists.**\
  Lists are set in the `[[list]]` section, and one list can be used in several ipsets.
- New commands: `print-dnsmasq-config` and `upgrade-config`. The second one upgrades an old config by itself when the package is upgraded.
- The `dnsmasq_lists_dir` option was removed.

## keenetic-pbr 1.x {#v1}

The first versions, under the name keenetic-pbr.

### 1.3.0 {#v1-3-0}

_15 January 2025_

- **Fewer writes to flash.**\
  keenetic-pbr does not overwrite files that have not changed. This spares the router's flash memory and USB drive.
- The `summarize` option was removed: because of a bug it dropped needed addresses from the list.

### 1.2.0 {#v1-2-0}

_8 January 2025_

- New commands: `interfaces` (list of interfaces), `self-check` (checks routes and rules) and `undo-routing` (removes everything keenetic-pbr set up).
- The `file` option adds a list from a local file.
- **Kill switch.**\
  The `kill_switch` option blocks the addresses from the lists when the VPN is down.
- **Several interfaces.**\
  The `interface` option was replaced by `interfaces`: you can list several interfaces, and the first one that is up is used.
- The `use_keenetic_api` option (on by default) checks through {{< term "RCI" >}} whether an interface has a connection.
- `apply` re-reads the lists and sets up the routes every time. You no longer need to restart OPKG or the interfaces.

### 1.0.1 {#v1-0-1}

_17 November 2024_

- ipset is created even if the lists have no IP addresses.
- Experimental IPv6 support.

### 1.0.0 {#v1-0-0}

_11-16 November 2024_

- First release. Follow-up builds fixed the `download` command messages for local lists, the default dnsmasq config, and the installer, which no longer replaces an existing `dnsmasq.conf`.
