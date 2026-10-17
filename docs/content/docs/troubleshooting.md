---
title: Troubleshooting
weight: 7
aliases:
  - /docs/troubleshooting/troubleshooting/
---

Start with the **General** > **Dashboard** page in the web interface: it shows whether the service is running and what exactly is broken. If that is not enough, read the system log: `keen-pbr` writes the reason for every error there.

Each section below follows the same order: symptom, check in the web interface, check by command, fix.

## Quick check in the web interface

Open `http://<router-ip>:12121/` and go to **General** > **Dashboard**.

1. The **status bar** at the top. "Everything works" means there are no problems. "Service is stopped" means routing is off: click **Start**. "System is running, but there are N problems" or "Routing works with limitations" means you should look at the **Diagnostics** panel.
2. The **Outbounds** panel. An outbound is a channel that traffic goes through: a VPN interface, your ISP, or a block. Each channel should be "Healthy". "Degraded" means the channel works partially, "Unavailable" means it does not work.
3. The **Diagnostics** panel. If everything is fine it says "No problems". To see healthy items too, turn on **Show healthy**. For what each message means, see [Messages in Diagnostics](#diagnostics-messages).
4. The **Where does this traffic go?** button (Ctrl+Alt+K) in the page header. Enter a domain or IP address and click **Check route**: keen-pbr shows which rule matches and which outbound the traffic takes.
5. The **Domain-based routing** panel with the DNS check widget. Click **Check again**: "DNS interception from this browser is working" means DNS is fine.

The same overview is available on the router:

```bash {filename="bash"}
keen-pbr status
```

It prints the state of outbounds, firewall rules, the kernel, DNS interception and dnsmasq, then the list of problems. The `Overall:` line at the end is the summary. The exit code is 1 if a check failed. The statuses `MISSING`, `MISMATCH` and `ERROR` show what exactly is wrong.

If you cannot solve the problem yourself, click **Download report** in the **Diagnostics** panel and attach the file to your request. The report contains your configuration, so read the "Warning: sensitive data" notice first. The option "Hide list contents and list URLs" removes list contents from the report.

## System Log

This is the first place to look. Search for `keen-pbr` messages and the markers `[E]` (error) and `[W]` (warning). If system packages are missing, an interface is not found, the web interface port is taken, or the JSON is broken, the reason is written here.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
In the Keenetic web interface, open **Diagnostics** → **System log**.

From the console you can read the log like this:

```bash {filename="bash"}
ndmc -c "show log once" | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
logread | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
journalctl -u keen-pbr
```

For the current boot only:

```bash {filename="bash"}
journalctl -u keen-pbr -b
```
{{< /tab >}}
{{< /tabs >}}

## Service Does Not Start {#service-does-not-start}

**Symptom:** the web interface does not open, or the status bar shows "Service is stopped".

**In the web interface:** if it opens, click **Start** or **Restart** in the status bar on the **Dashboard** page. If it does not open, use the commands.

**Commands.** Do not edit the configuration blindly: check the status first and read the log.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
1. Make sure the configuration exists:
   ```bash {filename="bash"}
   ls -l /opt/etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr restart
   ```
3. Check the `keen-pbr` status:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr status
   ```
4. Read the system log:
   ```bash {filename="bash"}
   ndmc -c "show log once" | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
1. Make sure the configuration exists:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   service keen-pbr restart
   ```
3. Check the `keen-pbr` status:
   ```bash {filename="bash"}
   service keen-pbr status
   ```
4. Read the system log:
   ```bash {filename="bash"}
   logread | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="Debian" >}}
1. Make sure the configuration exists:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   systemctl restart keen-pbr
   ```
3. Check the `keen-pbr` status:
   ```bash {filename="bash"}
   systemctl status keen-pbr
   ```
4. Read the system log:
   ```bash {filename="bash"}
   journalctl -u keen-pbr -b
   ```
{{< /tab >}}
{{< /tabs >}}

**Fix.** If `keen-pbr` does not stay running, stop the service and start `keen-pbr` manually with verbose logging. The error appears right in the console.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr stop
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service keen-pbr stop
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl stop keen-pbr
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< /tabs >}}

While `keen-pbr` is running, the command does not return to the prompt. That is expected. If it exits right away, the last error in the output is the cause.

{{% details title="Advanced checks" closed="true" %}}
1. Validate the JSON:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
jq . /opt/etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< /tabs >}}

2. Make sure the directory for `daemon.pid_file` exists and is writable.
3. Make sure `daemon.cache_dir` exists and is writable.
4. If the web interface is enabled, check that the address and port from `api.listen` are not used by another process.
5. If the log shows a firewall error, check that `iptables` and `ipset`, or `nftables`, are installed for your platform.
6. If `keen-pbr` is running but the web interface does not open, make sure `config.json` has an `api` section.
{{% /details %}}

## Sites Are Not Going Through the VPN

**Symptom:** a site from a list opens directly instead of through the VPN.

**In the web interface.**

1. Click **Where does this traffic go?** on the **Dashboard** page, enter the domain and click **Check route**. To check a specific device, enter its IP in the device IP field. The result tells you what is wrong:
   - "IP not in the expected ipset" (an ipset is a set of addresses that a rule uses to find the right traffic): keen-pbr has not learned the site's address yet. Open the site on the device, clear the device's DNS cache, and check that the browser does not use DoH/DoT (encrypted DNS that bypasses the router).
   - "No rule matched. The system route is used.": the domain is in no list, or the list is not used in a routing rule. Check **Routing** > **Lists** and **Routing** > **Routing rules**.
   - "The route differs from the expected route": a rule exists but traffic goes elsewhere. See the firewall and routing table sections below.
2. Make sure the right channel is "Healthy" in the **Outbounds** panel.
3. Open **General** > **Settings** and check three options:
   - **Enable domain-based routing** (the **Domain-based routing** card) must be on, otherwise domain rules do not work. IP-based rules work without it.
   - **Processed (inbound) interfaces** (the **General** card): if the list is not empty, only traffic from these interfaces is processed, and devices behind other interfaces go directly. Add the LAN interface you need or clear the list.
   - **Process router's own traffic** (shown in **Advanced** settings mode) (the **General** card) is off by default. Traffic from the router itself, such as `curl` or `ping` from the SSH console, is not affected by the rules. Test routing from a device on your local network.

**Commands.** On the router or server:

```bash {filename="bash"}
keen-pbr test-routing google.com
```

The command prints a table: IP address, matched list, expected and actual outbound, and status. To check a specific device, add `--src-addr <device-ip>`.

**Fix.** If the expected and actual outbound differ, check in order: DNS (next section), firewall, and routing tables. If they match but the site still does not open through the VPN, check the tunnel itself in "Interfaces and VPN Tunnels".

## Messages in Diagnostics {#diagnostics-messages}

The **Diagnostics** panel on the **Dashboard** page lists the problems found. The **Restart** button in the status bar re-applies the settings.

| Message | What to do |
|---|---|
| "Firewall chain is missing", "Firewall chain is not hooked into PREROUTING" | Click **Restart** in the status bar. If the message stays, read the log: the cause is usually missing permissions or missing `iptables` / `nftables` packages. |
| "Firewall rules do not match: N", "Routing tables do not match: N", "Policy rules do not match: N" | Click **Restart**. If the message comes back, another program is changing the rules: see "Low-Level Routing Conflicts". |
| "{tag} is unavailable", "{tag} is degraded", "{tag}: failing members: N" | The channel `{tag}` does not answer the health checks. See "Interfaces and VPN Tunnels". |
| "dnsmasq is not running", "dnsmasq does not serve the DNS rules", "DNS rules are being repaired" | See "Per-list DNS servers do not work". |
| "Kernel module is missing and the current rules need it: the firewall rules cannot be applied" | Rules that need the module will not be applied until the module is present on the system. |
| "Traffic interception is limited" | Look at the state in the **Domain-based routing** panel and read the log. |
| "Routing checks failed" | Click **Restart** and read the log. |
| "{interface}: no NAT (MASQUERADE/SNAT) for this WAN" | Outgoing traffic leaves through this interface without address translation, and replies do not come back. Enable NAT on the interface. For iptables, for example: `iptables -t nat -A POSTROUTING -o <interface> -j MASQUERADE`. If NAT is set up in native nftables rules, you can ignore the warning. |
| "{interface}: strict rp_filter may drop replies" | Set the kernel parameters `net.ipv4.conf.all.rp_filter` and `net.ipv4.conf.<interface>.rp_filter` to 2 or 0, for example `sysctl -w net.ipv4.conf.all.rp_filter=2`. |
| "A foreign rule uses packet mark bits reserved for keen-pbr" | Another program uses the packet marks that keen-pbr needs. Open **General** > **Settings** > **Advanced routing settings** (the card is visible when **Settings mode** = **Advanced**) and change **fwmark starting value** and **fwmark mask** to a free range. |

## DNS interception

For domain-based routing, keen-pbr reads the DNS answers that pass through the router and remembers the addresses of sites from your lists. No separate DNS server is needed for this.

**Symptom:** sites do not go through the VPN by domain, but do by IP address.

**In the web interface.** On the **Dashboard** page, in the **Domain-based routing** panel, find the rows **DNS interception**, **DNS hold** and **L7 interception**. The status "Not running" or "Disabled" explains why interception does not work. Below, click **Check again** in the DNS check widget and compare the result:

- "DNS interception from this browser is working": everything is fine.
- "Browser request completed, but the interceptor did not see the marker lookup.": the browser reaches DNS bypassing the router. Turn off encrypted DNS (DoH) in the browser and try again.
- "DNS interception is disabled in config.": turn on **Intercept DNS server responses** in **General** > **Settings** > **Domain-based routing**.
- "Interception is not working: the DNS hold is not active": click **Restart** in the status bar, then read the log.
- "The browser could not open the live DNS event stream…": close extra keen-pbr tabs and try again.

To check another device, click **Test from another device**. The dialog shows a command `nslookup <random-name>.check.keen-pbr.fyi`: run it on a phone or computer. If "DNS request from the device was observed by the interceptor." appears, the device uses the router's DNS. If it says "The DNS test query has not arrived yet…", make sure the device uses the router's DNS and run the command again.

**Command.** On the user's device:

```bash {filename="bash"}
nslookup check.keen-pbr.fyi
```

The domain also has a public address, so the answer shows what happened:

- `127.0.0.88`: the query went through the router and keen-pbr saw it.
- Any other address: DNS works but bypasses the router. For example, the browser or OS uses its own encrypted DNS.
- No answer or an error: DNS does not work on the device, or the router's resolver does not respond.

### Checking the resolver on the router

Check that the router's resolver answers locally:

```bash {filename="bash"}
nslookup google.com 127.0.0.1
```

If you see `Connection refused`, the resolver is not running or is not listening on `127.0.0.1:53`. Check its service and log: {{< term "Entware" >}} `dnsmasq` on Keenetic, `dnsmasq` on OpenWrt and Debian, or the built-in Keenetic DNS proxy.

On Keenetic, if client DNS queries do not reach Entware `dnsmasq`, check the `opkg dns-override` setting. The commands `opkg dns-override` and `system configuration save` are run in the router's web console at `http://my.keenetic.net/a`, not over SSH. Details are in [installing on Keenetic]({{< relref "/docs/getting-started/installation/keenetic" >}}).

{{% details title="After upgrading from an older keen-pbr version" closed="true" %}}
After the upgrade, dnsmasq management stays enabled; see [Breaking Changes in 3.4.0]({{< relref "/docs/changelog#upgrading-from-33x" >}}). If dnsmasq does not start after you removed keen-pbr manually, make sure its configuration has no leftover `conf-script=...keen-pbr...` line.
{{% /details %}}

## Websites Are Not Opening: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

keen-pbr does not answer clients' DNS queries, so check the resolver your clients use.

1. Make sure the device uses the router's DNS.
2. Make sure the router's resolver has working upstream servers (external DNS it forwards to) and can reach them.
3. Check from the router: `nslookup google.com 127.0.0.1`.
4. If a DNS server in **DNS** > **DNS servers** has a channel set (`detour` in JSON), check that this outbound is "Healthy" in the **Outbounds** panel.

## Websites Are Not Opening: `DNS_PROBE_FINISHED_BAD_CONFIG`

This usually means the resolver is not running or could not apply its configuration. Check the resolver's service and log: {{< term "Entware" >}} `dnsmasq` on Keenetic, `dnsmasq` on OpenWrt and Debian. Then check the **Domain-based routing** panel on the **Dashboard** page.

## Per-list DNS servers do not work {#per-list-dns-servers-do-not-work}

**Symptom:** domains from a list should resolve through the chosen DNS server, but resolve through the default one.

**In the web interface.**

1. Open **General** > **Settings** > **Local DNS server management** and make sure **Manage dnsmasq configuration** is on.
2. On the **Dashboard** page, look at the **DNS Rules** panel. "In sync" means everything is applied. "Applying" and "Reconciling" are temporary. For "Error", look at "Last error" and the message under it: it names the cause and suggests what to do. For example, if it says "Apply or Restart re-arms automatic repair", click one of them.
3. If you have several dnsmasq instances, pick the right one in the **dnsmasq instance** field in the same card.

**Command.**

```bash {filename="bash"}
keen-pbr resolver dnsmasq detect
```

The command shows the dnsmasq instances found, their DNS addresses, and the instance keen-pbr uses. In a healthy state keen-pbr is shown as `installed`, and the `problem` line says `none`. If dnsmasq is not installed and you do not need per-list DNS servers, turn off **Manage dnsmasq configuration**. Otherwise install dnsmasq.

**Fix.**

1. If several dnsmasq instances are found, pick the right one in **dnsmasq instance** and apply the configuration.
2. Make sure clients really use dnsmasq as their resolver. On Keenetic check `opkg dns-override` (see [installing on Keenetic]({{< relref "/docs/getting-started/installation/keenetic" >}})). From a client:
   ```bash {filename="bash"}
   nslookup example.com
   ```
3. If the DNS server has a channel set (`detour`, for example `vpn`), make sure this outbound is "Healthy" in the **Outbounds** panel.
4. Make sure the domains are really in the list. Check them in **Routing** > **Lists** and **DNS** > **DNS Rules**.

## Firewall and `KeenPbrTable`

**Symptom:** DNS works, the domain resolves, but traffic bypasses the VPN.

**In the web interface.** In the **Diagnostics** panel, look for messages about the chain, firewall rules and policies (see the message list above).

**Command.** The main check:

```bash {filename="bash"}
keen-pbr status
```

Lines with the statuses `MISSING`, `MISMATCH` or `ERROR` in the Firewall block show what is missing.

{{% details title="Look at the firewall rules directly" closed="true" %}}
`keen-pbr` creates its own chain or table `KeenPbrTable`. Traffic enters it, is matched against the lists, and gets a packet mark ({{< term "fwmark" >}}), which the system uses to choose a routing table.

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
```

There should be a jump from `PREROUTING` to `KeenPbrTable` and rules with `MARK`. An example of a correct rule:

```text
-A KeenPbrTable -m set --match-set <set> dst -j MARK --set-xmark <mark>/<mask>
```

To check the contents of a set:

```bash {filename="bash"}
ipset list
ipset test <set_name> <IP-address>
```

Expected answer: the IP is in the given set.
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset
```

For the full output including set contents:

```bash {filename="bash"}
nft list ruleset
```

There should be a table `inet KeenPbrTable`, a `prerouting` hook, rules that check sets, and the action `meta mark set ...`.
{{< /tab >}}
{{< tab name="Debian" >}}
The backend depends on how the firewall is installed on your system. Check both variants, or the one named in the `keen-pbr` log.

```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
nft -t list ruleset
```
{{< /tab >}}
{{< /tabs >}}
{{% /details %}}

**Fix.** If `KeenPbrTable` is missing, click **Restart** in the status bar and read the log. The cause is usually an unavailable backend, missing permissions, missing packages, or a configuration error.

## Routing Tables and `fwmark`

**Symptom:** the firewall sets a packet mark ({{< term "fwmark" >}}), but the site loads forever or opens through your ISP. The system must see the mark, apply an `ip rule`, and send the packet to the routing table of the right outbound.

**In the web interface.** In the **Diagnostics** panel, look for "Routing tables do not match" and "Policy rules do not match".

**Commands.**

1. Check the state `keen-pbr` expects:
   ```bash {filename="bash"}
   keen-pbr status
   ```
2. Check a specific domain or IP. The expected and actual outbound should match:
   ```bash {filename="bash"}
   keen-pbr test-routing google.com
   ```
3. Check the `ip rule` entries. There should be a rule like `fwmark <mark> lookup <table>`:
   ```bash {filename="bash"}
   ip rule show
   ```
4. Check the routing table. It should contain a route through the right VPN interface or gateway. For a blocking outbound (`blackhole`), expect a `blackhole` route:
   ```bash {filename="bash"}
   ip route show table <table_number>
   ```

**Fix.** If the table is empty, the interface is not found, or the rule points the wrong way, click **Restart** in the status bar. If that does not help, check the outbound name and interface name, and mark conflicts (see "Low-Level Routing Conflicts").

## Routing Breaks When Tailscale Is Enabled {#tailscale}

Tailscale reserves `0x00FF0000`, which overlaps the default keen-pbr mask, and
restores {{< term "connmark" >}} from {{< term "conntrack" >}}, fully overwriting the packet's
{{< term "fwmark" >}}. Moving keen-pbr to a different fwmark range does not stop Tailscale from erasing it.

Disable Tailscale's netfilter integration:

```bash {filename="bash"}
tailscale set --netfilter-mode=off
```

After that, firewall and {{< term "NAT" >}} rules must be configured separately, especially if
Tailscale runs as a subnet router or exit node.

## Interfaces and VPN Tunnels

**Symptom:** DNS, firewall and routing look right, but traffic does not go through the VPN.

**In the web interface.** Open **General** > **Dashboard** and look at the **Outbounds** panel: the channel should be "Healthy", and its interface "Active" or "Backup".

**Commands.**

1. Check that the interface exists and is up:
   ```bash {filename="bash"}
   ip link show
   ip addr show
   ip route
   ```
2. Check egress through a specific VPN interface:
   ```bash {filename="bash"}
   curl -v --interface <interface_name> https://ifconfig.co/json
   ```

Normally `curl` returns the VPN's external IP. If the command hangs or fails, the problem is not in `keen-pbr`: the tunnel is down, there is no route, the gateway is unreachable, or outgoing traffic is blocked. Fix the VPN itself.

{{% details title="If you use urltest or icmptest" closed="true" %}}
`urltest` and `icmptest` are outbounds that choose the best of several channels by probing them on a schedule.

1. Check that the channels (outbounds from `outbound_groups`) have the right interfaces or tables.
2. Look at the state of each channel in the **Outbounds** panel on the **Dashboard** page or on the **Routing** > **Outbounds** page.
3. If a channel is "Unavailable", keen-pbr excluded it after failed probes. Wait for `circuit_breaker.timeout_ms` or fix the channel.
4. If a backup channel does not take over, first check each channel separately with `curl --interface`.
{{% /details %}}

## Remote Lists Do Not Update

**Symptom:** a list with a `url` is empty or outdated.

**In the web interface.** Open **Routing** > **Lists** and click **Update** next to the list, or **Update all**. If the button is inactive, apply the draft configuration first.

**Command.** On the router or server:

```bash {filename="bash"}
keen-pbr download
```

**Fix.**

1. Check that the URL is reachable from this same system.
2. If the list must be downloaded through the VPN, check `lists[].detour` and the matching outbound.
3. If automatic update is used, check **Refresh schedule** in **General** > **Settings** > **Lists autoupdate**.
4. If a list failed to download at the first start, keen-pbr keeps retrying in the background: after 10 seconds, 30 seconds, 2 minutes, then every 5 minutes, until the download succeeds. If the domain of the list server must resolve through a separate DNS server, add that domain to an inline list, a file list, or an already cached list, otherwise the first attempt goes through the default DNS.
5. After a failure, read the `keen-pbr` log.

{{% details title="Forced full reload" closed="true" %}}
This command downloads all remote lists again and re-applies the rules:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr reload
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service keen-pbr reload
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl reload keen-pbr
```
{{< /tab >}}
{{< /tabs >}}

Also make sure `daemon.cache_dir` is writable.
{{% /details %}}

## `urltest` always shows Degraded

1. Make sure the test `url` is reachable and returns a `2xx` code, for example `200 OK` or `204 No Content`. The default is `https://www.gstatic.com/generate_204`.
2. If HTTPS probes fail on the router (wrong system time, outdated root certificates, TLS limitations), try an HTTP address, for example `http://www.gstatic.com/generate_204`.
3. Make sure each channel works on its own.
4. Check the interfaces with `curl --interface <interface_name> https://ifconfig.co/json`.
5. Wait for the next probe, or temporarily lower `interval_ms` while diagnosing.
6. Look at the **Outbounds** panel on the **Dashboard** page. If a channel is "Unavailable", keen-pbr excluded it. Wait for `circuit_breaker.timeout_ms`.

## Rules with port or address filters do not work

{{< callout type="warning" >}}
When using negation in `src_addr` / `dest_addr` fields, the negation applies to all IP addresses/subnets specified in that field. You cannot mix negated and non-negated entries in the same list. Create two separate rules instead.

The same applies to `src_port` / `dest_port`.
{{< /callout >}}

If rules are not matching as expected:

- Make sure the protocol is set correctly: any (`null` in JSON), `"tcp"`, `"udp"`, or `"tcp/udp"`.
- Check that the list name in the rule exactly matches the key in `lists`, including case.
- Click **Where does this traffic go?** on the **Dashboard** page, set **Protocol**, **Destination port** and the device IP as in the rule, and look at the result. `keen-pbr test-routing <domain-or-ip>` does the same, with the options `--proto`, `--src-addr`, `--src-port`, `--dest-port`, `--dscp`.
- Run `keen-pbr status` and check whether firewall rules were created for this routing rule.

## Low-Level Routing Conflicts

If another program on the router also sets packet marks ({{< term "fwmark" >}}) or manages `ip rule`, packets can go the wrong way or be dropped. The message "A foreign rule uses packet mark bits reserved for keen-pbr" in the **Diagnostics** panel warns about this.

Check for conflicts:

```bash {filename="bash"}
keen-pbr status
ip rule show
ip route show table all
```

For firewall rules with marks:

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
ip6tables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
```
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset | grep -E 'mark|KeenPbrTable'
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
iptables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
ip6tables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
nft -t list ruleset | grep -E 'mark|KeenPbrTable'
```
{{< /tab >}}
{{< /tabs >}}

Pick a free mark range. In the web interface open **General** > **Settings** > **Advanced routing settings** (the card is visible when **Settings mode** = **Advanced**) and change **fwmark starting value** and **fwmark mask**. In JSON it looks like this:

```json {filename="config.json"}
{
  "fwmark": {
    "start": "0x00020000",
    "mask": "0x00FF0000"
  }
}
```

The mask must consist of consecutive hexadecimal `F` digits. Values are hex strings, for example `"0x00FF0000"` and `"0x00020000"`.

## Measuring losses in DNS interception

{{% details title="For advanced users" closed="true" %}}
If the first connection to a freshly opened site sometimes bypasses the VPN, you can measure how often keen-pbr fails to remember the address before the DNS answer reaches the device. The script `dns-bench.py` does this. It is not included in the keen-pbr packages: download it from the [repository](https://github.com/maksimkurb/keen-pbr/blob/main/scripts/dns-bench.py). It needs Python 3. Run it from a computer on the local network:

```bash {filename="bash"}
python3 dns-bench.py --api http://192.168.1.1:12121 --password 'admin-password' \
  --rates 50,200,1000 --count 1000
```

The script queries random names like `a1b2c3d4-198-18-7-9.nip.io`. They point to the test addresses `198.18.0.0/15`, so ordinary traffic is not affected. For keen-pbr to process them, add a list and a rule to the configuration (if they are missing, the script tells you what to add):

```json
"lists": { "dns_bench": { "domains": ["nip.io"] } },
"route": { "rules": [ { "list": ["dns_bench"], "outbound": "<any outbound tag>" } ] }
```

The public `nip.io` service may rate-limit requests; then `client-timeout` appears in the report.

Main results in the report:

| Result | Meaning |
|---|---|
| `held-ok` | All good: the address was recorded before the answer went to the device |
| `hold-timeout` | keen-pbr was not done within `intercept.dns.hold_timeout_ms`: the answer went out first and the address was added slightly later. The first packets could bypass the VPN |
| `set-error` | The address could not be written to the set; see the keen-pbr log |
| `bypass` | The answer bypassed keen-pbr: the name is not in the lists or the queue overflowed |
| `not-in-set`, `wrong-set` | After the run the address is not in the set, or is in the set of another outbound |
| `client-timeout` | The device got no answer |

If there are many `hold-timeout` results, the router lacks CPU to record addresses. Increase **DNS hold timeout (milliseconds)** in **General** > **Settings** > **Domain-based routing**. `python3 dns-bench.py --help` lists all script options.
{{% /details %}}
