---
title: Troubleshooting
weight: 7
aliases:
  - /docs/troubleshooting/troubleshooting/
---

Start with logs and service status, then move to DNS, firewall, routing tables, and interfaces. In `keen-pbr` 3.x the daemon writes failure reasons to the system log, so logs are usually faster than guessing from symptoms.

## Quick Diagnostic Order

1. Check the system log for `keen-pbr` errors.
2. Check that the `keen-pbr` services are running.
3. If `keen-pbr` crashes during startup, run it manually in the terminal to see more logs: `keen-pbr --log-level verbose service`.
4. Check DNS: the user device must use the router DNS; the configured resolver must answer locally.
5. Check the firewall: `keen-pbr` rules must be present in `KeenPbrTable`.
6. Check {{< term "policy routing" >}}: the `fwmark` must point to the expected routing table.
7. Check interfaces and VPN tunnels.
8. If the problem remains, check remote lists, `urltest`, rule filters, and low-level routing conflicts.

## System Log

This is the first place to look. Search for `keen-pbr` and the `[E]` / `[W]` levels: `keen-pbr` uses those prefixes for errors and warnings. If libraries for `iptables` / `nftables` are missing, an interface is not found, the API port is busy, or JSON is broken, the reason is usually written here.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
In the Keenetic Web UI, open **Diagnostics** -> **System log**.

From the console, read the log like this:

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

If you only need the current boot:

```bash {filename="bash"}
journalctl -u keen-pbr -b
```
{{< /tab >}}
{{< /tabs >}}

## Service Does Not Start

In this scenario, do not start by editing the config blindly. Check status first, then read the logs immediately.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
1. Confirm that the config exists:
   ```bash {filename="bash"}
   ls -l /opt/etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr restart
   ```
3. Check `keen-pbr` status:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr status
   ```
4. Read the system log:
   ```bash {filename="bash"}
   ndmc -c "show log once" | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
1. Confirm that the config exists:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   service keen-pbr restart
   ```
3. Check `keen-pbr` status:
   ```bash {filename="bash"}
   service keen-pbr status
   ```
4. Read the system log:
   ```bash {filename="bash"}
   logread | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="Debian" >}}
1. Confirm that the config exists:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Restart `keen-pbr`:
   ```bash {filename="bash"}
   systemctl restart keen-pbr
   ```
3. Check `keen-pbr` status:
   ```bash {filename="bash"}
   systemctl status keen-pbr
   ```
4. Read the system log:
   ```bash {filename="bash"}
   journalctl -u keen-pbr -b
   ```
{{< /tab >}}
{{< /tabs >}}

If `keen-pbr` does not stay running, stop the service and run keen-pbr manually in the terminal with verbose logs. The error will appear directly in the console.

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

While keen-pbr is running, the command does not return to the shell prompt; this is normal. If it exits immediately, the last error in the output is usually the cause.

{{% details title="Advanced checks" closed="true" %}}
1. Validate JSON:

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
4. If the API is enabled, check that the address and port from `api.listen` are not already used by another process.
5. If logs mention a firewall backend error, check that `iptables` / `ipset` or `nftables` is installed for your platform.
6. If `keen-pbr` is alive but Web UI does not open, make sure the config was not replaced with a headless-only example and that `config.json` contains the `api` section.
{{% /details %}}

## Sites Are Not Going Through the VPN

1. Make sure the user device is using the router DNS.
   - Open `http://<router-ip>:12121/` and look at the DNS Check widget. The marker request should reach the router resolver and return `127.0.0.88`.
   - Alternatively, run this from your PC: `nslookup check.keen-pbr.fyi`. It should return `127.0.0.88`.
2. Run a routing test:
   - Open `http://<router-ip>:12121/` and enter a domain or IP into the "Where does this traffic go?" widget.
   - Alternatively, run this from the router or server:
     ```bash {filename="bash"}
     keen-pbr test-routing google.com
     ```
3. Make sure the domain or IP is in the correct list.
4. Make sure the route rule for that list points to the expected outbound.
5. Make sure the VPN interface is actually up and passing traffic.

If the expected and actual outbounds differ, continue through the DNS, firewall, and routing sections below.

## DNS interception

Domain routing does not need any particular resolver: keen-pbr reads DNS replies passing through the router itself and writes the addresses to list sets. Check `/api/health/service` (`intercept`) for the interception state and the reasons it may be turned off.

### Check DNS From the User Device

Open `http://<router-ip>:12121/` and check DNS Check. If Web UI is unavailable, run this from the client device:

```bash {filename="bash"}
nslookup check.keen-pbr.fyi
```

Expected response: `127.0.0.88`. The domain also has a real public address, so the answer tells you what happened:

- `127.0.0.88`: the request went through the router and keen-pbr saw it.
- Any other address: DNS works, but bypasses the router (for example, the browser or OS uses its own secure DNS), or DNS interception is unavailable.
- No answer or an error: the device has no working DNS, or the router resolver is not answering.

### Check the Router Resolver

keen-pbr works with whatever resolver the router uses. Check that it answers locally:

```bash {filename="bash"}
nslookup google.com 127.0.0.1
```

If you see `Connection refused`, your resolver is not running or is not listening on `127.0.0.1:53`. Check its own service and log (for example {{< term "Entware" >}} `dnsmasq` on Keenetic, `dnsmasq` on OpenWrt or Debian, or the built-in Keenetic DNS proxy).

On Keenetic, if client DNS requests do not reach Entware `dnsmasq`, check the Keenetic-only setting:

```bash {filename="bash"}
opkg dns-override
```

After changing it, save the Keenetic configuration:

```bash {filename="bash"}
system configuration save
```

{{% details title="After upgrading from an older keen-pbr" closed="true" %}}
Older keen-pbr versions always configured dnsmasq. The package upgrade removes those settings and restores your DNS servers; see [Upgrading from 3.3.x]({{< relref "/docs/changelog#upgrading-from-33x" >}}). On Keenetic, review the `# BEGIN keen-pbr fallback upstream` block in `/opt/etc/dnsmasq.conf` if it is there. If dnsmasq refuses to start, check that no `conf-script=...keen-pbr...` line is left in its configuration.
{{% /details %}}

### Measuring DNS interception loss

If the first connection to a freshly opened site sometimes goes around the VPN, check how often keen-pbr fails to write the address before the DNS reply reaches the device. The `scripts/dns-bench.py` script does this (only Python 3 is needed). Run it from a computer on your LAN or from the router itself:

```bash {filename="bash"}
python3 scripts/dns-bench.py --api http://192.168.1.1:12121 --password 'admin-password' \
  --rates 50,200,1000 --count 1000
```

The script queries random names like `a1b2c3d4-198-18-7-9.nip.io`. They point to the test range `198.18.0.0/15`, so real traffic is not affected. For keen-pbr to handle them, add a list and a rule to the configuration (if they are missing, the script tells you what to add):

```json
"lists": { "dns_bench": { "domains": ["nip.io"] } },
"route": { "rules": [ { "list": ["dns_bench"], "outbound": "<any outbound tag>" } ] }
```

The public `nip.io` service may rate-limit queries; this shows up as `client-timeout` in the report.

Main results in the report:

| Result | Meaning |
|---|---|
| `held-ok` | All good: the address was written before the reply reached the device |
| `hold-timeout` | keen-pbr did not make it within `intercept.dns.hold_timeout_ms`: the reply went out first and the address was written slightly later. The first packets may have bypassed the VPN |
| `set-error` | The address could not be written to the set; check the keen-pbr log |
| `bypass` | The reply did not pass through keen-pbr: the name is not in any list, or the queue overflowed |
| `not-in-set`, `wrong-set` | After the run the address is missing from the set, or is in another outbound's set |
| `client-timeout` | The device got no reply |

Many `hold-timeout` results mean the router lacks CPU to write addresses in time: try increasing `intercept.dns.hold_timeout_ms`. `python3 scripts/dns-bench.py --help` describes every option.

## Websites Are Not Opening: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

keen-pbr does not resolve names for clients, so check the resolver the clients use.

1. Make sure the user device is using the router DNS.
2. Make sure the router resolver has working upstream servers and can reach them.
3. Test it from the router: `nslookup google.com 127.0.0.1`.
4. If a `dns.servers` entry uses `detour`, check the selected outbound.

## Websites Are Not Opening: `DNS_PROBE_FINISHED_BAD_CONFIG`

This usually means the resolver is not running or failed to apply its configuration. Check the resolver service and log on your platform (for example {{< term "Entware" >}} `dnsmasq` on Keenetic, `dnsmasq` on OpenWrt and Debian), and the interception state (`intercept`) in `/api/health/service`.

## Per-list DNS servers do not work

When `dns.resolver_integration: "dnsmasq"` is enabled, domains from specific lists should be resolved through a chosen DNS server. If this is not happening:

1. **Check the dnsmasq state in `/api/health/service`:**
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/health/service | jq .dnsmasq
   ```
   Expected: `"state": "ok"`, `"rules"` shows the count, `"domains"` shows domain entries. If `state` is `error`, check `last_error`.

2. **Verify the generated dnsmasq config:**
   ```bash {filename="bash"}
   keen-pbr generate-resolver-config dnsmasq
   ```
   Expected: domain-to-server mappings, `fallback` servers, and {{< term "DNS rebinding" >}} protection exceptions. If output is empty, check that `resolver_integration: "dnsmasq"` is set and at least one rule is configured.

3. **Check the dnsmasq drop-in file:**

   {{< tabs >}}
   {{< tab name="OpenWrt" selected=true >}}
   ```bash {filename="bash"}
   cat /tmp/dnsmasq.d/keen-pbr-upstream-dns.conf
   # or in a dnsmasq-specific confdir:
   cat /tmp/dnsmasq.*/keen-pbr-upstream-dns.conf
   ```
   {{< /tab >}}
   {{< tab name="Keenetic / Entware" >}}
   ```bash {filename="bash"}
   grep -A 5 "BEGIN keen-pbr" /opt/etc/dnsmasq.conf
   ```
   {{< /tab >}}
   {{< tab name="Debian" >}}
   ```bash {filename="bash"}
   cat /etc/dnsmasq.d/keen-pbr-upstream-dns.conf
   ```
   {{< /tab >}}
   {{< /tabs >}}

   Expected: a `conf-script=keen-pbr generate-resolver-config dnsmasq` line.

4. **Check that dnsmasq really loaded the keen-pbr config:**
   ```bash {filename="bash"}
   nslookup -type=txt config-hash.keen.pbr 127.0.0.1
   ```
   Expected: a TXT answer `<hash>|<boottime_ms>|<unix_ts>`; the hash must match `config_hash` from step 1. No answer means dnsmasq did not load the keen-pbr config (for example, the {{< term "drop-in" >}} is in a directory dnsmasq does not read, or dnsmasq was not restarted).

5. **Verify dnsmasq is actually the resolver clients use:**
   - On Keenetic, check if ndnproxy or dnsmasq is the LAN resolver:
     ```bash {filename="bash"}
     opkg dns-override
     ```
   - Test from a client:
     ```bash {filename="bash"}
     nslookup example.com
     ```

6. **Check the DNS server's `detour`:**
   If the server has `"detour": "vpn"`, verify the VPN interface is up and reachable:
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/runtime/outbounds | jq '.[] | select(.tag == "vpn")'
   ```
   Expected: `"state": "alive"`.

7. **Verify domains are in the right list:**
   - Open the Web UI at `http://<router-ip>:12121/` and check DNS rules.
   - Confirm the domains you want are actually in the list.

## Firewall and `KeenPbrTable`

If DNS works and the domain resolves, but traffic still bypasses the VPN, check the firewall. `keen-pbr` creates an isolated chain or table named `KeenPbrTable`; traffic must enter it, match configured lists, and receive the correct `fwmark`.

First run the built-in check:

```bash {filename="bash"}
keen-pbr status
```

Look for firewall checks with `MISSING`, `MISMATCH`, or `ERROR` status.

### Check Firewall Rules

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
```

Expected result: there is a jump from `PREROUTING` to `KeenPbrTable` and rules that set a {{< term "fwmark" >}} (`MARK`). Example of a healthy rule:

```text
-A KeenPbrTable -m set --match-set <set> dst -j MARK --set-xmark <mark>/<mask>
```

To check set contents:

```bash {filename="bash"}
ipset list
ipset test <set_name> <IP-address>
```

Expected response for a match: the IP is in the specified set.
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset
```

If you need full output including set contents:

```bash {filename="bash"}
nft list ruleset
```

Expected result: there is an `inet KeenPbrTable` table, a `prerouting` hook, rules matching sets, and a `meta mark set ...` action.
{{< /tab >}}
{{< tab name="Debian" >}}
The backend depends on how firewall is installed on your system. Check both variants or the one mentioned in `keen-pbr` logs.

```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
nft -t list ruleset
```
{{< /tab >}}
{{< /tabs >}}

If `KeenPbrTable` is missing, return to `keen-pbr` logs: the cause is usually an unavailable backend, permissions, missing packages, or a configuration error.

## Routing Tables and `fwmark`

If the firewall sets a {{< term "fwmark" >}} on packets but the site loads forever or opens through the ISP, check {{< term "policy routing" >}}. The OS must see the `fwmark`, apply an `ip rule`, and send the packet to the routing table for the expected outbound.

1. Check the state expected by `keen-pbr`:
   ```bash {filename="bash"}
   keen-pbr status
   ```
   Look for lines with `MISSING`, `MISMATCH`, or `ERROR` status.

2. Check a specific domain or IP:
   ```bash {filename="bash"}
   keen-pbr test-routing google.com
   ```
   Expected result: the expected and actual outbounds match.

3. Check the `ip rule` entries:
   ```bash {filename="bash"}
   ip rule show
   ```
   Expected result: there is a rule like `fwmark <mark> lookup <table>`.

4. Check the routing table:
   ```bash {filename="bash"}
   ip route show table <table_number>
   ```
   Expected result: the table has a route through the expected VPN interface or gateway. For a blackhole outbound, a blackhole route is expected.

If the table is empty, the interface is not found, or the rule points to the wrong place, check the outbound name, interface name, and `fwmark` / `iproute.table_start` conflicts.

## Routing Breaks When Tailscale Is Enabled {#tailscale}

Tailscale reserves `0x00FF0000`, which overlaps the default keen-pbr mask, and
restores the {{< term "connmark" >}} from {{< term "conntrack" >}} by overwriting the whole {{< term "fwmark" >}} of the
packet. Moving keen-pbr to another fwmark range does not prevent Tailscale
from erasing it.

Disable Tailscale's {{< term "netfilter" >}} integration:

```bash {filename="bash"}
tailscale set --netfilter-mode=off
```

Firewall and {{< term "NAT" >}} rules must then be managed separately, especially when
Tailscale operates as a subnet router or exit node.

## Interfaces and VPN Tunnels

If DNS, firewall, and {{< term "policy routing" >}} look correct, check that the interface itself exists, is up, and can send traffic.

1. Open `http://<router-ip>:12121/` and check runtime state for outbounds and interfaces.
2. Get the interface list through the REST API:
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/runtime/interfaces
   ```
3. Compare with system state:
   ```bash {filename="bash"}
   ip link show
   ip addr show
   ip route
   ```
4. Check egress through a specific VPN interface:
   ```bash {filename="bash"}
   curl -v --interface <interface_name> https://ifconfig.co/json
   ```

Expected result: `curl` returns the external VPN IP. If the command hangs or exits with an error, the problem is below `keen-pbr`: the tunnel is down, there is no route, the gateway is unreachable, or outbound traffic is blocked.

{{% details title="If you use urltest or icmptest" closed="true" %}}
1. Check that the links (the outbounds listed in `outbound_groups`) have correct interfaces or tables.
2. Check link states on the **Outbounds** page of the web UI or in `GET /api/runtime/outbounds`.
3. If a link is shown as `unavailable`, keen-pbr has excluded it after failed checks. Wait for `circuit_breaker.timeout_ms` or fix the link.
4. If the backup link does not take over, first check each link separately with `curl --interface`.
{{% /details %}}

## Remote Lists Do Not Update

1. Run on the router or server:
   ```bash {filename="bash"}
   keen-pbr download
   ```
2. If the list still does not update, check whether the URL is reachable from the same system.
3. If the list should be downloaded through VPN, check `lists[].detour` and the corresponding outbound.
4. If automatic refresh is used, check `lists_autoupdate.cron`.
5. If the list failed on the first start, the daemon retries it in the background (after 10 seconds, 30 seconds, 2 minutes, then every 5 minutes) until it succeeds. With per-list DNS servers, dnsmasq is configured at startup before the first download. If the list host's domain should be resolved through a per-list DNS server, put that domain in an inline list, a file list, or an already cached list; otherwise the first attempt uses the normal DNS.
6. After an error, read `keen-pbr` logs again.

{{% details title="Advanced checks" closed="true" %}}
If you need to force a full reload:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

If your config sets a different `daemon.pid_file`, use that path. Also confirm that `daemon.cache_dir` is writable.
{{% /details %}}

## `urltest` always shows Degraded

1. Make sure the test `url` is reachable and returns a `2xx` status, such as `200 OK` or `204 No Content`. The default URL is `https://www.gstatic.com/generate_204`.
2. If HTTPS checks fail on the router (wrong system time, outdated CA certificates, TLS limitations), try an HTTP URL instead, for example `http://www.gstatic.com/generate_204`.
3. Make sure each link works on its own.
4. Check interfaces with `curl --interface <interface_name> https://ifconfig.co/json`.
5. Wait for the next check, or temporarily lower `interval_ms` during diagnostics.
6. Check `GET /api/runtime/outbounds`. If a link is `unavailable`, keen-pbr has excluded it; wait for `circuit_breaker.timeout_ms`.

## Rules with port or address filters do not work

{{< callout type="warning" >}}
When using negation in `src_addr` / `dest_addr` fields, the negation applies to all IP addresses/subnets specified in that field. Mixing entries with negation and without negation in the same list is not supported. As an alternative, you can create two separate rules.

This is also valid for `src_port` / `dest_port`.
{{< /callout >}}

If rules are not matching as expected:

- Verify that `proto` is set correctly: `null` for any protocol, `"tcp"`, `"udp"`, or `"tcp/udp"`.
- Check that the list name in the rule exactly matches the key in `lists`, including case.
- Run `keen-pbr test-routing <domain-or-ip>` and compare expected / actual.
- Check `keen-pbr status` to see whether firewall rules were created for this rule.

## Low-Level Routing Conflicts

If you changed `fwmark` or `iproute`, or another tool manages {{< term "policy routing" >}} on the same system, packets may be misrouted or dropped.

Check for conflicts:

```bash {filename="bash"}
keen-pbr status
ip rule show
ip route show table all
```

For {{< term "fwmark" >}} rules in the firewall:

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

Move `fwmark` to a non-conflicting range:

```json {filename="config.json"}
{
  "fwmark": {
    "start": "0x00020000",
    "mask": "0x00FF0000"
  }
}
```

`mask` must be a run of hex `F` digits. Use hex strings such as `"0x00FF0000"` and `"0x00020000"`.
