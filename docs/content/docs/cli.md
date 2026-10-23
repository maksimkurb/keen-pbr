---
title: keen-pbr CLI usage
weight: 4
aliases:
  - /docs/advanced/cli/
  - /docs/advanced/signals/
---

`keen-pbr` can run the service, inspect live routing state, download list data, and test routing decisions.

The `status`, `download` and `test-routing` commands talk to the running service. If the service is not running, they fail with exit code 1. Start the service first (on a router it starts with the system).

## Usage

```text
Usage: keen-pbr [options] <command>

Options:
  --config <path>    Path to JSON config file (default: <KEEN_PBR_DEFAULT_CONFIG_PATH>)
  --log-level <lvl>  Log level: error, warn, info, verbose, debug (default: info)
  --log-target <target>  Log destination: stderr, syslog, or both (default: both)
  --pid-file <path>  Override daemon.pid_file when running the service command
  --crash-report <path>  Last-crash report path (default: /tmp/keen-pbr-crash.log)
  --no-api           Disable REST API at runtime
  --use-raw-prerouting  Use raw PREROUTING for IPv4 forwarded traffic (iptables only)
  --use-raw6-prerouting Use raw PREROUTING for IPv6 forwarded traffic (iptables only)
  --json             Print JSON on stdout instead of text (status, download, test-routing)
  --version          Show version and exit
  --help             Show this help and exit

Commands:
  service                            Start the routing service (foreground)
  status [--json]                    Show routing/firewall status and exit
  download [--reload] [--json]       Download all configured lists to cache and exit
  test-routing <ip-or-domain> [packet options] [--json] [-v|--verbose]
                                     Test expected vs actual routing for an IP or domain
      --proto <tcp|udp|other>        Packet protocol
      --src-addr <ip>                Packet source address
      --src-port <port>              Packet source port
      --dest-port <port>             Packet destination port
      --dscp <0-63>                  Packet DSCP (default: 0)
      -v, --verbose                  Also print the per-rule table
  resolver dnsmasq print-config      Print the dnsmasq config generated from dns.rules/dns.fallback (cached lists only; used as dnsmasq conf-script, always exits 0)
  resolver dnsmasq detect [--json]   List dnsmasq instances, their DNS endpoints and which one keen-pbr uses
  hash-password [--update]           Generate an authentication password hash; --update writes config.json
```

The config file is usually `/etc/keen-pbr/config.json` on OpenWrt and Debian, and `/opt/etc/keen-pbr/config.json` on Keenetic / NetCraze.

## Options

| Flag | Description |
|---|---|
| `--config <path>` | Path to the JSON config file. Works only with the `service` command. |
| `--log-level <lvl>` | Log verbosity: `error`, `warn`, `info`, `verbose`, or `debug`. |
| `--log-target <target>` | External log destination: `stderr`, `syslog`, or `both`. Defaults to `both` for every command. The packaged service files (systemd, OpenWrt procd, Keenetic init script) pass `--log-target syslog` so logs are not duplicated when stdout/stderr are also captured by the init system. |
| `--pid-file <path>` | Path to the PID file (the file holding the service's process number). Replaces `daemon.pid_file` from the config; `service` only. |
| `--crash-report <path>` | Where to write the report about the last crash. Defaults to `/tmp/keen-pbr-crash.log`. |
| `--no-api` | Disable the REST API even if enabled in config. |
| `--use-raw-prerouting` | {{< only iptables >}} Opt in to raw-table IPv4 forwarded-traffic classification. |
| `--use-raw6-prerouting` | {{< only iptables >}} Opt in to raw-table IPv6 forwarded-traffic classification. |
| `--json` | Print the result as JSON on stdout instead of text. Works with `status`, `download` and `test-routing`; exit codes stay the same. |
| `--version` | Print version and exit. |
| `--help` | Print help and exit. |

### `--use-raw-prerouting`

This flag is mainly for Keenetic / NetCraze. The firmware periodically rebuilds the `mangle` firewall table, and keen-pbr's rules briefly disappear. With this flag keen-pbr puts the rules for IPv4 traffic passing through the router into the `raw` table, which the firmware does not rebuild. IPv6 has a separate flag, `--use-raw6-prerouting`.

On Keenetic / NetCraze the start script sets these flags. The mode is set with `KEEN_PBR_RAW_PREROUTING` in `/opt/etc/keen-pbr/defaults`:

| Value | What it does |
|---|---|
| `auto` (default) | Checks separately for IPv4 and IPv6 whether `raw` works, and uses it if so; otherwise `mangle` |
| `disable` | Always use `mangle` |
| `enable` | Require `raw` for IPv4 and IPv6. If `raw` is unavailable, the service does not start |
| `ipv4-only`, `ipv6-only` | Require `raw` for one address family only |

Restart the service after changing it. The practical difference between the modes is described in [Keenetic / NetCraze installation]({{< relref "/docs/getting-started/installation/keenetic" >}}).

To check whether `raw` is available:

```sh
ls -l "/lib/modules/$(uname -r)/iptable_raw.ko"
ls -l "/lib/modules/$(uname -r)/ip6table_raw.ko"
grep -x raw /proc/net/ip_tables_names
grep -x raw /proc/net/ip6_tables_names
iptables -t raw -S
ip6tables -t raw -S
```

## Commands

| Command | Description |
|---|---|
| `service` | Start the routing service in the foreground. |
| `status` | Show service, outbound, routing, firewall and kernel health, then the issue list the web UI overview shows, and exit. Exits with 1 when any check failed or any issue is an error. |
| `download [--reload]` | Download all URL-backed lists into the cache and exit. With `--reload` the service applies the lists that changed right away. |
| `resolver dnsmasq print-config` | Print the dnsmasq configuration for per-list DNS servers. |
| `hash-password [--update]` | Create a password hash for logging in to the web UI and API. |
| `resolver dnsmasq detect [--json]` | List dnsmasq instances, their DNS endpoints and the instance keen-pbr uses. |
| `test-routing <ip-or-domain> [packet options]` | Compare expected and actual routing for the given IP or domain. The daemon runs the check, the same way as the routing test in the web UI. |

### `resolver dnsmasq print-config`

When `dns.resolver_integration: "dnsmasq"` is enabled, this command prints the dnsmasq configuration built by keen-pbr. Lists are taken from the cache only; nothing is downloaded. It contains domain-to-server mappings, {{< term "DNS rebinding" >}} protection exceptions, and `fallback` servers.

**Usage:**

```bash
keen-pbr resolver dnsmasq print-config
```

**Output:** Dnsmasq configuration directives.

```text
# keen-pbr generated, do not edit

address=/use-application-dns.net/

no-resolv
# direct -> [DNS] 192.168.1.1
server=192.168.1.1
# dns proxy :52100 -> [DoH] https://dns.google/dns-query
server=127.0.0.89#52100

# List: ai_services

# direct -> [DNS] 1.1.1.1
server=/openai.com/www.openai.com/.../1.1.1.1
rebind-domain-ok=/openai.com/www.openai.com/...

# List: blocked

# dns proxy :52101 -> [DoH] https://dns.google/dns-query via vpn, [DoH] https://cloudflare-dns.com/dns-query
server=/example.com/.../127.0.0.89#52101
```

**Behavior:**
- Always exits with status 0, even on errors; errors are printed as `# keen-pbr: ...` comment lines.
- When `resolver_integration: "none"` (default), or the config cannot be read, the output is only the `server=` lines from `dnsmasq-fallback.conf`; the dynamic sets are filled by interception.
- With `"dnsmasq"` and no `fallback` servers, those `server=` lines are added to the output.
- dnsmasq reads the config via this command (see `conf-script=keen-pbr generate-resolver-config dnsmasq` in the {{< term "drop-in" >}}).

### `hash-password`

Creates a password hash for logging in to the web UI and API. The command asks for the password twice.

```bash
keen-pbr hash-password
keen-pbr hash-password --update
```

Without `--update` the command prints `Password hash: <hash>`, which you paste into `api.authentication.password_hash` yourself. With `--update` it writes `api.enabled: true`, `api.authentication.enabled: true` and the hash into `config.json`. Then restart the service: `Restart the keen-pbr service for authentication changes to take effect.`

### `resolver dnsmasq detect` {#resolver-dnsmasq-detect}

Inspect the dnsmasq integration without the daemon. The command does not need the control socket.

```bash
keen-pbr resolver dnsmasq detect [--json]
```

The command lists every dnsmasq instance the platform hook reports: whether it runs, its DNS endpoint and how it was confirmed, listening sockets, whether keen-pbr's {{< term "drop-in" >}} is installed and which configuration it loaded, and any problem. The last line tells which instance keen-pbr selected and how (`explicit`, `adopted` or `automatic`), or why it could not choose.

```text
dnsmasq hook: /usr/lib/keen-pbr/dnsmasq-hook.sh (platform openwrt, binary /usr/sbin/dnsmasq)

  uci:cfg01411c  [selected: adopted]
    running:     yes (pid 1842)
    enabled:     yes
    endpoint:    127.0.0.1:53 (stamp_verified)
    listeners:   udp+tcp 0.0.0.0:53, udp+tcp [::]:53
    keen-pbr:    installed, loaded hash 3f2a9c0e1b7d4a55c6e8f0a1b2c3d4e5
    conf-dir:    /tmp/dnsmasq.cfg01411c.d (included)
    problem:     none

Selection: uci:cfg01411c (adopted)
```

- `--json` prints the same data as one JSON object (`selected`, `selection`, `error_code`, `message`, `warnings`, `instances[]`).
- Exit code `0` when an instance is selected and has no problem, `1` otherwise.
- If the choice is ambiguous, the output ends with a hint to set `dns.dnsmasq_instance`.

## Colors {#cli-colors}

When the output goes to a terminal, `keen-pbr` colors it: green for good, yellow for warnings, red for errors. Colors are off when the output is piped or redirected, when `TERM` is not set or is `dumb`, and when the `NO_COLOR` variable is set to a non-empty value.

The output is plain ASCII. Status marks are written as `[ok]`, `[!!]` and `[xx]`. `--json` output never contains colors.

## Signals

You can control the running `keen-pbr` service with signals:

| Signal | Action |
|---|---|
| `SIGUSR1` | Rebuild routes and firewall rules |
| `SIGHUP` | Re-read `config.json` and apply it again. Does not work in the `keen-pbr-headless` build |
| `SIGTERM` / `SIGINT` | Graceful shutdown |

Example: re-read the config with SIGHUP:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

## Examples

Check live routing and firewall state:

```bash {filename="bash"}
keen-pbr status
```

Example output:

```text
keen-pbr status - config: /etc/keen-pbr/config.json
Service: running (runtime running: startup complete) - keen-pbr 3.4.0 (build 20261012064940)
Firewall backend: nftables

Outbounds:
  corp_vpn [interface] iface=corp_vpn fwmark=0x00010000 table=402
    state ........................................................... HEALTHY
    route   table=402 default dev corp_vpn ............................... OK
    rule    0x00010000/0x00ff0000 -> table=402 pri=402 ........... OK [v4+v6]
  auto_vpn [urltest] fwmark=0x00060000 table=402
    state ........................................................... HEALTHY
    member  corp_vpn (corp_vpn) 41 ms ................................ ACTIVE
    member  backup_vpn (wg1) ....................................... DEGRADED
      probe failed: timeout
    route   table=402 default dev corp_vpn ............................... OK
    rule    0x00060000/0x00ff0000 -> table=402 pri=407 ........... OK [v4+v6]

Firewall:
  chain   KeenPbrTable / prerouting hook ............................... OK
  rule    kpbr4_generic -> MARK 0x00010000 ........................ MISSING
    rule not found in nftables prerouting chain
  rule    kpbr4_auto -> MARK 0x00060000 selected=corp_vpn .............. OK

Kernel 6.12.94:
  capability nfqueue ................................................... OK
  probe      set_write ................................................. OK
  probe      conntrack ................................................. OK

Interception: running, DNS hold active, L7 active
dnsmasq: ok, alive, 127.0.0.1:53 (uci:cfg01411c), 3 rules, 16 domains

Issues:
  ERROR  Firewall rules do not match: 1
         kpbr4_generic: rule not found in nftables prerouting chain
  WARN   auto_vpn: failing members: 1
         backup_vpn · degraded · probe failed: timeout

Overall: DEGRADED (1 check(s) failed, 1 error(s), 1 warning(s))
Status values: OK / MISSING / MISMATCH / ERROR
```

Download all URL-backed lists:

```bash {filename="bash"}
keen-pbr download
```

The command prints one line per list and a summary. A list that failed shows the reason; in that case the command exits with code 1:

```text
[ok] google  updated
- apple   unchanged
[xx] broken  HTTP 503

3 lists: 1 updated, 1 unchanged, 1 failed
```

With `--json` the command prints one object with the outcome of every list; a failed list also gets the reason. The exit code is the same, so with failures the object is printed and the code is 1:

```bash {filename="bash"}
keen-pbr download --json
```

```json
{
  "lists": [
    {"list": "google", "status": "changed"},
    {"list": "apple", "status": "unchanged"},
    {"list": "broken", "status": "failed", "error": "HTTP 503"}
  ],
  "summary": {"refreshed": 3, "changed": 1, "unchanged": 1, "failed": 1},
  "reloaded": false
}
```

`status --json` prints the service state, outbounds, issues and routing health, in the same shape as the REST endpoints `GET /api/health/service`, `GET /api/runtime/outbounds`, `GET /api/health/issues` and `GET /api/health/routing`. `test-routing --json` prints the same body as the routing test in the REST API. Messages for people go to stderr, so stdout can be piped to `jq`.

Compare expected and actual routing:

```bash {filename="bash"}
keen-pbr test-routing google.com
```

The result is a trace of the same steps as the routing test in the web interface: the verdict, DNS resolution, list matching, rule selection and the firewall check. When every address routes as expected, each step takes one line:

```text
[ok] The routes match the rules - google.com

  [ok] DNS resolution - 2001:4860:4860::8888  142.250.74.14
  [ok] List matching - google (entry google.com)
  [ok] Rule selection - Rule #1 -> corp_vpn
  [ok] Firewall check - 2 of 2 IPs match - corp_vpn

  This checks the route from firewall state, not website connectivity.
```

When something is wrong, the steps are expanded, addresses with the same outcome are grouped, and the output ends with an explanation and what to do:

```text
[!!] IP not in the expected ipset - yandex.ru

  [ok] DNS resolution
      77.88.44.55  77.88.55.88  5.255.255.77
      Resolved by dnsmasq 127.0.0.1:53 (uci:cfg01411c)
      This check uses the router's DNS. Devices that use DoH, DoT or their own DNS server may get different addresses.
  [ok] List matching
      russia (entry ru)
  [ok] Rule selection
      Rule #8 -> home_auto
  [!!] Firewall check - 0 of 3 IPs match
      Expected  home_auto
      Actual    wan_loadbalance  balance: wan1 83% - wan2 17%
      - The IPs are not in this rule's ipset.
      - keen-pbr has no record of adding them.

  The domain is in the list, but its IPs are not in the rule's ipset yet. keen-pbr adds addresses when a client looks the domain up through the router's DNS.
    1. Open this site on the client device and run the check again.
    2. Still missing - clear the DNS cache on the device and in the browser, then try again.
    3. Still missing - make sure the device does not use third-party DNS (DoH, DoT or its own DNS server) that keen-pbr cannot intercept. ...
```

Add `-v` or `--verbose` to also print the table with one row per address and the details of the rules and outbound groups.

By default only list membership is checked. To also match rules that use ports, protocol, source address or DSCP, describe the packet:

| Flag | Description |
|---|---|
| `--proto <tcp\|udp\|other>` | Packet protocol. |
| `--src-addr <ip>` | Source address of the client sending the packet. |
| `--src-port <port>` | Source port. |
| `--dest-port <port>` | Destination port. |
| `--dscp <0-63>` | DSCP value. A packet without `--dscp` is treated as DSCP 0. |
| `-v`, `--verbose` | Also print the table with one row per address. |

```bash {filename="bash"}
keen-pbr test-routing google.com --proto tcp --dest-port 443 --src-addr 192.168.1.10
```

If a rule depends on a field you did not give (for example `src_addr`), the check says the route cannot be determined yet and names the flag to pass.
