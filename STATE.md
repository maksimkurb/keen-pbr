# keen-pbr3 Architecture & State Document

**Version:** 3.0.0
**Language:** C++20
**Target platforms:** OpenWRT (MIPS, ARM, AArch64, x86_64), Keenetic (MIPS LE)

## Table of Contents

1. [Overview](#overview)
2. [High-Level Architecture](#high-level-architecture)
3. [Module Dependency Graph](#module-dependency-graph)
4. [Data Flow](#data-flow)
5. [Configuration Schema](#configuration-schema)
6. [CLI Interface](#cli-interface)
7. [Startup Sequence](#startup-sequence)
8. [Event Loop & Scheduling](#event-loop--scheduling)
9. [List Management](#list-management)
10. [Routing Pipeline](#routing-pipeline)
11. [Firewall Backends](#firewall-backends)
12. [Health Checking & Circuit Breaker](#health-checking--circuit-breaker)
13. [DNS Routing & Dnsmasq Integration](#dns-routing--dnsmasq-integration)
14. [REST API](#rest-api)
15. [Shutdown Sequence](#shutdown-sequence)
16. [Build System](#build-system)
17. [Cross-Compilation](#cross-compilation)
18. [CI/CD Pipeline](#cicd-pipeline)
19. [File Layout](#file-layout)
20. [External Commands Reference](#external-commands-reference)

---

## Overview

keen-pbr3 is a policy-based routing daemon for Linux routers. It enables selective routing of traffic based on destination IP addresses, CIDR subnets, and domain names. Traffic matching configured lists is marked with firewall marks (fwmarks) and routed through specific network interfaces, routing tables, or blackholed.

Key capabilities:
- Download and parse IP/domain lists from URLs, local files, or inline config
- Create kernel ipsets/nft sets and populate them with IPs/CIDRs
- Generate dnsmasq config for domain-based ipset population
- Install fwmark-based ip rules and routes via netlink
- ICMP health checking with circuit breaker for interface failover
- Periodic list refresh and on-demand reload via SIGUSR1
- Optional REST API for status, health, and reload

```plantuml
@startuml overview
!theme plain
skinparam backgroundColor white

package "keen-pbr3 Daemon" {
  [Config Parser] as config
  [List Manager] as lists
  [Firewall\n(iptables/nftables)] as fw
  [Routing\n(netlink)] as routing
  [Health Checker] as health
  [DNS Router] as dns
  [Dnsmasq Generator] as dnsmasq
  [Event Loop\n(epoll)] as daemon
  [Scheduler\n(timerfd)] as scheduler
  [REST API\n(optional)] as api
}

cloud "Remote Lists" as remote
file "Local Files" as local
file "config.json" as configfile
file "dnsmasq.conf" as dnsmasqfile

database "Linux Kernel" {
  [ipset / nft sets] as ipsets
  [iptables / nft rules] as rules
  [ip rules\n(policy routing)] as iprules
  [routes\n(per-table)] as routes
}

component "dnsmasq" as dnsmasq_proc

configfile --> config
config --> lists
config --> fw
config --> routing
config --> dns

remote --> lists : HTTP(S)
local --> lists : file I/O

lists --> fw : IPs/CIDRs
fw --> ipsets : create/populate
fw --> rules : mark rules

routing --> iprules : fwmark→table
routing --> routes : default routes

dns --> dnsmasq : domains→servers
dnsmasq --> dnsmasqfile : write config
dnsmasq_proc ..> ipsets : populates via\nipset= directives

health --> daemon : check results
scheduler --> daemon : timer fds
api ..> daemon : status/reload
@enduml
```

---

## High-Level Architecture

```plantuml
@startuml architecture
!theme plain
skinparam backgroundColor white
skinparam componentStyle rectangle

package "src/config/" {
  [config.hpp/cpp\nJSON parser] as ConfigParser
  [list_parser.hpp/cpp\nIP/domain parser] as ListParser
}

package "src/http/" {
  [http_client.hpp/cpp\nlibcurl wrapper] as HttpClient
}

package "src/lists/" {
  [list_manager.hpp/cpp\ndownload + cache] as ListManager
  [ipset.hpp/cpp\nbinary trie] as IpSet
}

package "src/routing/" {
  [target.hpp/cpp\nroute resolution] as Target
  [netlink.hpp/cpp\nlibnl3 wrapper] as Netlink
  [route_table.hpp/cpp\nroute tracking] as RouteTable
  [policy_rule.hpp/cpp\nrule tracking] as PolicyRule
}

package "src/firewall/" {
  [firewall.hpp/cpp\nabstract + factory] as FirewallBase
  [iptables.hpp/cpp\nipset + iptables] as Iptables
  [nftables.hpp/cpp\nnft CLI] as Nftables
}

package "src/health/" {
  [health_checker.hpp/cpp\nICMP ping] as HealthChecker
  [circuit_breaker.hpp/cpp\nstate machine] as CircuitBreaker
}

package "src/dns/" {
  [dns_server.hpp/cpp\ntype parser] as DnsServer
  [dns_router.hpp/cpp\nrule matching] as DnsRouter
  [dnsmasq_gen.hpp/cpp\nconfig writer] as DnsmasqGen
}

package "src/daemon/" {
  [daemon.hpp/cpp\nepoll + signalfd] as Daemon
  [scheduler.hpp/cpp\ntimerfd] as Scheduler
}

package "src/api/" <<optional>> {
  [server.hpp/cpp\ncpp-httplib] as ApiServer
  [handlers.hpp/cpp\nGET/POST handlers] as ApiHandlers
}

[src/main.cpp\nentry point] as Main

Main --> ConfigParser
Main --> ListManager
Main --> FirewallBase
Main --> Netlink
Main --> RouteTable
Main --> PolicyRule
Main --> Target
Main --> HealthChecker
Main --> CircuitBreaker
Main --> DnsRouter
Main --> DnsmasqGen
Main --> Daemon
Main --> Scheduler
Main --> ApiServer

ListManager --> HttpClient
ListManager --> ListParser
DnsRouter --> DnsServer
DnsmasqGen --> DnsRouter
DnsmasqGen --> ListManager
RouteTable --> Netlink
PolicyRule --> Netlink
Iptables --|> FirewallBase
Nftables --|> FirewallBase
Scheduler --> Daemon
ApiHandlers --> ApiServer
@enduml
```

---

## Module Dependency Graph

```plantuml
@startuml dependencies
!theme plain
skinparam backgroundColor white
left to right direction

rectangle "main.cpp" as main
rectangle "config" as config #LightBlue
rectangle "list_parser" as lp #LightBlue
rectangle "http_client" as http #LightGreen
rectangle "list_manager" as lm #LightGreen
rectangle "ipset (trie)" as ipset #LightGreen
rectangle "target" as target #Orange
rectangle "netlink" as netlink #Orange
rectangle "route_table" as rt #Orange
rectangle "policy_rule" as pr #Orange
rectangle "firewall" as fw #Pink
rectangle "iptables" as ipt #Pink
rectangle "nftables" as nft #Pink
rectangle "health_checker" as hc #Yellow
rectangle "circuit_breaker" as cb #Yellow
rectangle "dns_server" as ds #Cyan
rectangle "dns_router" as dr #Cyan
rectangle "dnsmasq_gen" as dg #Cyan
rectangle "daemon" as daemon #Gray
rectangle "scheduler" as sched #Gray
rectangle "api/server" as api #Violet
rectangle "api/handlers" as ah #Violet

main --> config
main --> lm
main --> target
main --> fw
main --> netlink
main --> rt
main --> pr
main --> hc
main --> cb
main --> dr
main --> dg
main --> daemon
main --> sched
main --> api

lm --> http
lm --> lp
lm --> config

dr --> ds
dr --> lm

dg --> dr
dg --> lm
dg --> config

rt --> netlink
pr --> netlink

ipt --> fw
nft --> fw

sched --> daemon
ah --> api
@enduml
```

---

## Data Flow

```plantuml
@startuml dataflow
!theme plain
skinparam backgroundColor white

|Config|
start
:Read JSON config file;
:Parse config sections\n(daemon, api, outbounds,\nlists, dns, route);

|Lists|
:For each list definition:
 - Download URL (HTTP/HTTPS)
 - Read local file
 - Parse inline entries;
:Parse each source into\nParsedList (ips, cidrs, domains);
:Cache downloaded lists\nto /var/cache/keen-pbr3/;

|Firewall|
:For each route rule:;
:Resolve outbound via\nfailover chain + health check;
if (skip or no outbound?) then (yes)
  :Continue to next rule;
else (no)
  :For each list in rule:;
  :Create ipset/nft set\n(with optional TTL);
  :Add IPs and CIDRs\nto the set;
  :Create mark rule\n(fwmark 0x10000+);
endif

|Routing|
:For each resolved outbound:;
if (InterfaceOutbound?) then (yes)
  :Add ip rule:\nfwmark → table N;
  :Add default route\nin table N via interface;
elseif (TableOutbound?) then (yes)
  :Add ip rule:\nfwmark → existing table;
elseif (BlackholeOutbound?) then (yes)
  :Add ip rule:\nfwmark → table N;
  :Add blackhole route\nin table N;
endif

|DNS|
:Generate dnsmasq config:;
:For each list with domains:
 - ipset=/domain/setname
 - server=/domain/dns-ip;

|Runtime|
:Start epoll event loop;
:Schedule periodic tasks:
 - List refresh (24h default)
 - Health checks (per-outbound);
:Wait for events;
if (SIGUSR1?) then (yes)
  :Reload all lists;
elseif (SIGTERM/SIGINT?) then (yes)
  :Graceful shutdown;
elseif (Timer expired?) then (yes)
  :Execute scheduled callback;
endif

stop
@enduml
```

---

## Configuration Schema

The daemon reads a JSON configuration file (default: `/etc/keen-pbr3/config.json`).

### Top-Level Structure

```json
{
  "daemon": { ... },
  "api": { ... },
  "outbounds": [ ... ],
  "lists": { ... },
  "dns": { ... },
  "route": { ... }
}
```

### `daemon` Section

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `pid_file` | string | `""` | Path to PID file |
| `list_update_interval` | duration string | `"24h"` | How often to re-download lists |

Duration strings: `"30s"`, `"5m"`, `"24h"` (seconds/minutes/hours).

### `api` Section

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable REST API server |
| `listen` | string | `"127.0.0.1:8080"` | Listen address (host:port) |

### `outbounds` Section (array)

Each outbound has a `type` field that determines its variant:

**Interface Outbound** (`type: "interface"`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `tag` | string | required | Unique identifier |
| `interface` | string | required | Network interface name (e.g., `tun0`) |
| `gateway` | string | optional | Gateway IP for the interface |
| `ping_target` | string | optional | IP to ping for health checks |
| `ping_interval` | duration | `"30s"` | Health check interval |
| `ping_timeout` | duration | `"5s"` | Ping timeout |

**Table Outbound** (`type: "table"`):

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | Unique identifier |
| `table` | uint32 | Existing routing table ID |

**Blackhole Outbound** (`type: "blackhole"`):

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | Unique identifier |

### `lists` Section (object, keyed by list name)

| Field | Type | Description |
|-------|------|-------------|
| `url` | string | Remote list URL to download |
| `file` | string | Local file path |
| `domains` | string[] | Inline domain entries |
| `ip_cidrs` | string[] | Inline IP/CIDR entries |
| `ttl` | uint32/duration | TTL for dnsmasq-resolved ipset entries (seconds) |

All fields are optional. Sources are merged: URL content + file content + inline entries.

### `dns` Section

```json
{
  "servers": [
    { "tag": "my-dns", "address": "8.8.8.8", "detour": "vpn" }
  ],
  "rules": [
    { "list": ["list-name"], "server": "my-dns" }
  ],
  "fallback": "my-dns"
}
```

DNS server address types:
- **Plain IP**: `"8.8.8.8"`, `"2001:4860:4860::8888"`
- **DoH URL**: `"https://dns.google/dns-query"`
- **System**: `"system"` (use system resolver)
- **Blocked**: `"rcode://refused"` (refuse queries)

### `route` Section

```json
{
  "rules": [
    { "list": ["list-a", "list-b"], "outbound": "vpn" },
    { "list": ["list-c"], "outbounds": ["vpn", "wan"] },
    { "list": ["list-d"], "action": "skip" }
  ],
  "fallback": "wan"
}
```

Route rule actions (mutually exclusive):
- `"outbound": "tag"` — Route to single outbound
- `"outbounds": ["tag1", "tag2"]` — Failover chain (first healthy wins)
- `"action": "skip"` — Skip this rule (no routing applied)

---

## CLI Interface

```
Usage: keen-pbr3 [options]

Options:
  --config <path>  Path to JSON config file (default: /etc/keen-pbr3/config.json)
  -d               Daemonize (run in background)
  --no-api         Disable REST API at runtime
  --version, -v    Show version and exit
  --help, -h       Show help and exit

Commands:
  print-dnsmasq-config  Print generated dnsmasq config to stdout and exit
```

### `print-dnsmasq-config` Command

Special non-daemon mode: loads config, downloads/caches lists (readonly mode), generates dnsmasq config, prints to stdout, then exits. Useful for integration with dnsmasq's `conf-dir` or piping to a file.

---

## Startup Sequence

```plantuml
@startuml startup
!theme plain
skinparam backgroundColor white

start

:Parse CLI arguments;

if (--version?) then (yes)
  :Print version;
  stop
endif

if (--help?) then (yes)
  :Print usage;
  stop
endif

:Read config file;
:Parse JSON → Config struct;

if (print-dnsmasq-config?) then (yes)
  :Create ListManager (readonly);
  :Load lists from cache;
  :Create DnsRouter;
  :Create DnsmasqGenerator;
  :Print generated config to stdout;
  stop
endif

if (-d daemonize?) then (yes)
  :fork() → parent exits;
  :setsid() → new session;
  :Redirect stdio → /dev/null;
endif

:Write PID file;

partition "Initialize Subsystems" {
  :Create ListManager\n(/var/cache/keen-pbr3);
  :Create HealthChecker;
  :Create CircuitBreaker;
  :Create Firewall\n(auto-detect backend);
  :Create NetlinkManager\n(connect NETLINK_ROUTE);
  :Create RouteTable;
  :Create PolicyRuleManager;
}

:Register interface outbounds\nfor health checking;
:Download and load all lists;

partition "Apply Firewall & Routing" {
  :fwmark = 0x10000;
  repeat
    :Process next route rule;
    :Resolve outbound\n(health check + failover);
    if (skip or no outbound?) then (yes)
      :Skip;
    else (no)
      :Create ipsets for each list;
      :Add IPs/CIDRs to ipsets;
      :Create mark rule (fwmark);
      :Add ip rule (fwmark → table);
      :Add route (default via outbound);
      :fwmark++;
    endif
  repeat while (more rules?)
  :firewall→apply();
}

partition "Setup Event Loop" {
  :Create Daemon\n(epoll + signalfd);
  :Create Scheduler;
  :Register SIGUSR1 → reload;
  :Schedule periodic list update;
  :Schedule health checks;
  if (API enabled and not --no-api?) then (yes)
    :Create ApiServer;
    :Register API handlers;
    :Start API in background thread;
  endif
}

:daemon.run()\n(epoll_wait loop);

stop
@enduml
```

---

## Event Loop & Scheduling

```plantuml
@startuml eventloop
!theme plain
skinparam backgroundColor white

state "Daemon Event Loop" as loop {
  state "epoll_wait()" as wait
  state "Handle Signal" as signal
  state "Dispatch FD Callback" as dispatch

  wait --> signal : signalfd readable
  wait --> dispatch : timerfd / other fd readable
  signal --> wait : continue loop
  dispatch --> wait : continue loop
}

state "Signal Handling" as sighandling {
  state "SIGTERM / SIGINT" as sigterm
  state "SIGUSR1" as sigusr1

  sigterm : Set running_ = false
  sigterm : → epoll_wait returns
  sigterm : → daemon.run() exits

  sigusr1 : Call sigusr1_cb_()
  sigusr1 : → reload lists
}

state "Timer Handling" as timerhandling {
  state "Read timerfd" as readtimer
  state "Find entry by fd" as findentry
  state "Execute callback" as execcb

  readtimer --> findentry
  findentry --> execcb
  note right of execcb
    One-shot timers auto-remove
    after callback fires.
    Repeating timers stay registered.
  end note
}

loop --> sighandling
loop --> timerhandling
@enduml
```

### Implementation Details

- **Daemon** creates an `epoll` instance (`epoll_create1(EPOLL_CLOEXEC)`)
- Signals (SIGTERM, SIGINT, SIGUSR1) are blocked via `sigprocmask`, then handled through `signalfd`
- The signalfd is registered with epoll for edge notification
- External components (Scheduler timerfds) register via `add_fd(fd, events, callback)`
- `epoll_wait` blocks indefinitely (`timeout = -1`), EINTR is retried

### Scheduler

- Creates `timerfd` instances with `CLOCK_MONOTONIC` (immune to wall clock changes)
- Flags: `TFD_NONBLOCK | TFD_CLOEXEC`
- Repeating: sets both `it_value` and `it_interval` in `itimerspec`
- One-shot: sets only `it_value` (interval = {0,0})
- Must `read(fd, &uint64_t, 8)` to acknowledge timer, otherwise fd stays readable
- Registered timers:
  - **List refresh**: repeating, interval from `daemon.list_update_interval`
  - **Health checks**: one repeating timer per outbound with `ping_target`, interval from `ping_interval`

---

## List Management

```plantuml
@startuml lists
!theme plain
skinparam backgroundColor white

start
:ListManager::load();

partition "For each list definition" {
  if (URL configured?) then (yes)
    :HttpClient::download(url);
    if (download success?) then (yes)
      :Cache response to\n/var/cache/keen-pbr3/<name>.txt;
    else (no)
      if (cache file exists?) then (yes)
        :Read cached file;
      else (no)
        :throw exception;
      endif
    endif
  endif

  if (file configured?) then (yes)
    :Read local file contents;
  endif

  :Merge URL/file content;
  :ListParser::parse(content);
  note right
    For each non-empty, non-comment line:
    - Try IPv4 address → ips[]
    - Try IPv4 CIDR → cidrs[]
    - Try IPv6 address → ips[]
    - Try IPv6 CIDR → cidrs[]
    - Try domain name → domains[]
    - Unrecognized → skip
  end note

  if (inline ip_cidrs?) then (yes)
    :Parse and append\nto ips[] / cidrs[];
  endif

  if (inline domains?) then (yes)
    :Append to domains[];
  endif

  :Store as ParsedList;
}

stop
@enduml
```

### ParsedList Structure

```
ParsedList {
  ips: vector<string>      // Individual IP addresses (e.g., "1.2.3.4")
  cidrs: vector<string>    // CIDR subnets (e.g., "10.0.0.0/8")
  domains: vector<string>  // Domain names (e.g., "example.com", "*.example.org")
}
```

### IpSet (Binary Trie)

The `IpSet` class provides O(W) lookup (W = address width: 32 for IPv4, 128 for IPv6) using a binary trie (radix tree on individual bits). Separate tries for IPv4 and IPv6. Used internally but not currently integrated into the main traffic flow (firewall ipsets handle the actual matching in the kernel).

---

## Routing Pipeline

```plantuml
@startuml routing
!theme plain
skinparam backgroundColor white

start

partition "For each route rule (config order)" {
  :Get route rule action;

  if (action == skip?) then (yes)
    :RoutingDecision::skip();
    :Continue to next rule;
    stop
  endif

  if (action == single outbound tag?) then (yes)
    :Find outbound by tag;
    :RoutingDecision::route_to(outbound);
  endif

  if (action == failover chain?) then (yes)
    :For each tag in chain:;
    repeat
      :Find outbound by tag;
      if (health_fn provided?) then (yes)
        :Check circuit breaker;
        :Check health (ICMP ping);
        :Record success/failure;
      endif
      if (outbound healthy?) then (yes)
        :RoutingDecision::route_to(outbound);
        stop
      endif
    repeat while (more tags in chain?)
    :RoutingDecision::none();
  endif
}

partition "Apply to kernel" {
  :fwmark = 0x10000 + rule_index;
  :table_id = 100 + (fwmark & 0xFFFF);

  partition "Firewall" {
    :Create ipset/nft set\nfor each list;
    :Add IPs + CIDRs\nto the set;
    :Create mangle mark rule:\nmatch set → set fwmark;
  }

  partition "Routing (netlink)" {
    if (InterfaceOutbound) then
      :ip rule: fwmark → table_id;
      :ip route: default via gateway\ndev interface table table_id;
    elseif (TableOutbound) then
      :ip rule: fwmark → table.table_id;
    elseif (BlackholeOutbound) then
      :ip rule: fwmark → table_id;
      :ip route: blackhole default\ntable table_id;
    endif
  }
}

stop
@enduml
```

### Fwmark Allocation

- Starting mark: `0x10000`
- Each route rule gets the next sequential mark: `0x10000`, `0x10001`, `0x10002`, ...
- Table ID derived from mark: `100 + (fwmark & 0xFFFF)` → 100, 101, 102, ...
- Policy rule priority matches table ID

### Netlink Operations

All route/rule management goes through `NetlinkManager` which uses **libnl3**:

| Operation | libnl3 Function | Flags |
|-----------|----------------|-------|
| Add route | `rtnl_route_add()` | `NLM_F_CREATE \| NLM_F_REPLACE` (idempotent) |
| Delete route | `rtnl_route_delete()` | 0 |
| Add ip rule | `rtnl_rule_add()` | `NLM_F_CREATE \| NLM_F_EXCL` (fail on dup) |
| Delete ip rule | `rtnl_rule_delete()` | 0 |

When `family == 0`, rules are added for **both** AF_INET and AF_INET6.

### RouteTable & PolicyRuleManager

Both classes track installed specs in a vector:
- **Duplicate detection**: compare by value before adding
- **Cleanup**: remove in reverse order (LIFO)
- **Destructors**: best-effort cleanup (catch all exceptions)

---

## Firewall Backends

```plantuml
@startuml firewall
!theme plain
skinparam backgroundColor white

interface "Firewall" as fw {
  +create_ipset(name, family, timeout)
  +add_to_ipset(name, entry, timeout)
  +delete_ipset(name)
  +create_mark_rule(set, fwmark, chain)
  +delete_mark_rule(set, fwmark, chain)
  +apply()
  +cleanup()
}

class "IptablesFirewall" as ipt {
  -created_sets_: map<name, family>
  -mark_rules_: vector<MarkRule>
  +exec_cmd(cmd): int
  +exec_cmd_checked(cmd)
}

class "NftablesFirewall" as nft {
  -TABLE_NAME: "keen_pbr3"
  -table_inet_created_: bool
  -created_sets_: map<name, family>
  -created_chains_: map<key, bool>
  -mark_rules_: vector<MarkRule>
  +ensure_table(family)
  +ensure_chain(family, chain)
}

fw <|-- ipt
fw <|-- nft

note bottom of ipt
  Uses ipset + iptables CLI commands.
  Commands execute immediately (no staging).
  Cleanup: delete rules first, then ipsets.
end note

note bottom of nft
  Uses nft CLI commands.
  Single "inet" table for dual-stack.
  Cleanup: delete entire table (cascades).
end note
@enduml
```

### Backend Detection

```
create_firewall("auto"):
  1. command -v nft    → NftablesFirewall
  2. command -v iptables → IptablesFirewall
  3. Neither found → throw FirewallError
```

### iptables Backend — Shell Commands Executed

| Operation | Shell Command |
|-----------|--------------|
| Create ipset | `ipset create <name> hash:net family <inet\|inet6> [timeout <N>] -exist` |
| Add to ipset | `ipset add <name> <entry> [timeout <N>] -exist` |
| Flush ipset | `ipset flush <name> 2>/dev/null` |
| Destroy ipset | `ipset destroy <name> 2>/dev/null` |
| Create mark rule | `iptables -t mangle -A <chain> -m set --match-set <name> dst -j MARK --set-mark <0xHEX>` |
| Delete mark rule | `iptables -t mangle -D <chain> -m set --match-set <name> dst -j MARK --set-mark <0xHEX> 2>/dev/null` |
| (IPv6 variant) | Replace `iptables` with `ip6tables` |

**Cleanup order**: Delete all mark rules (reverse order) → destroy all ipsets.

### nftables Backend — Shell Commands Executed

| Operation | Shell Command |
|-----------|--------------|
| Create table | `nft add table inet keen_pbr3` |
| Create chain | `nft add chain inet keen_pbr3 <chain> '{ type filter hook prerouting priority mangle; policy accept; }'` |
| Create set | `nft add set inet keen_pbr3 <name> '{ type <ipv4_addr\|ipv6_addr>; flags <interval[, timeout]>; [timeout Ns;] }'` |
| Add element | `nft add element inet keen_pbr3 <name> '{ <entry> [timeout Ns] }'` |
| Create mark rule | `nft add rule inet keen_pbr3 <chain> ip daddr @<name> meta mark set <0xHEX>` |
| Flush set | `nft flush set inet keen_pbr3 <name> 2>/dev/null` |
| Delete set | `nft delete set inet keen_pbr3 <name> 2>/dev/null` |
| Delete rule (by handle) | `handle=$(nft -a list chain inet keen_pbr3 <chain> \| grep '@<name>' \| grep '<0xHEX>' \| sed 's/.*# handle //' \| head -1); if [ -n "$handle" ]; then nft delete rule inet keen_pbr3 <chain> handle $handle; fi` |
| Cleanup (all) | `nft delete table inet keen_pbr3 2>/dev/null` |

**Key differences from iptables**:
- Single `inet` family table handles both IPv4 and IPv6
- Sets use `flags interval` to support both IPs and CIDRs
- Cleanup is a single table delete (cascades to everything)

---

## Health Checking & Circuit Breaker

```plantuml
@startuml health
!theme plain
skinparam backgroundColor white

state "Health Check Flow" as flow {
  state "HealthChecker::check(tag)" as check
  state "Create SOCK_DGRAM\nICMP socket" as socket
  state "SO_BINDTODEVICE\nto interface" as bind
  state "Send ICMP Echo Request" as send
  state "poll() with timeout" as poll
  state "Receive & verify\nICMP Echo Reply" as recv

  check --> socket
  socket --> bind
  bind --> send
  send --> poll
  poll --> recv : data ready
  poll --> [*] : timeout → unhealthy

  recv --> [*] : ECHO_REPLY → healthy
  recv --> [*] : other → unhealthy
}

state "Circuit Breaker States" as cb {
  state "Closed\n(healthy)" as closed
  state "Open\n(blocked)" as open
  state "Half-Open\n(probing)" as halfopen

  closed --> open : failure_count >= threshold
  open --> halfopen : cooldown expired\n(checked in is_allowed)
  halfopen --> closed : probe success
  halfopen --> open : probe failure
  closed --> closed : success
}

note bottom of cb
  Default: threshold=3, cooldown=30s
  Uses steady_clock (monotonic)
end note
@enduml
```

### ICMP Ping Details

- Socket type: `SOCK_DGRAM` (not `SOCK_RAW`) — avoids `CAP_NET_RAW` requirement
- Protocol: `IPPROTO_ICMP` (v4) or `IPPROTO_ICMPV6` (v6)
- Interface binding: `SO_BINDTODEVICE` with interface name + null terminator
- IPv4 ICMP: manual checksum via RFC 1071 algorithm
- IPv6 ICMP: kernel computes checksum
- For `SOCK_DGRAM`, kernel strips IP header — first byte of recv'd data is ICMP header
- `poll()` used for timeout waiting (single fd)

### Health + Circuit Breaker Integration

In `main.cpp`, the health check function used during route resolution combines both:

```
health_fn(tag):
  if circuit_breaker.is_allowed(tag) == false → return false (circuit open)
  if health_checker.has_target(tag) == false → return true (no target = healthy)
  result = health_checker.check(tag) // actual ICMP ping
  record success or failure to circuit_breaker
  return result
```

---

## DNS Routing & Dnsmasq Integration

```plantuml
@startuml dns
!theme plain
skinparam backgroundColor white

package "DNS Configuration" {
  [DNS Servers\n(PlainIP, DoH, System, Blocked)] as servers
  [DNS Rules\n(list → server)] as rules
  [Fallback Server] as fallback
}

package "DnsRouter" {
  [Parse & validate\nserver configs] as validate
  [Match domain\nagainst list rules] as match
}

package "DnsmasqGenerator" {
  [Collect lists from\nroute rules (ipset)] as collectipset
  [Collect lists from\nDNS rules (server)] as collectdns
  [Generate ipset=\ndirectives] as genipset
  [Generate server=\ndirectives] as genserver
}

servers --> validate
rules --> match
validate --> match

collectipset --> genipset
collectdns --> genserver

note right of genipset
  ipset=/domain1/domain2/.../setname

  Only non-skip route rules
  generate ipset directives.
  Wildcard domains (*.x.com)
  stripped to base domain.
end note

note right of genserver
  server=/domain1/domain2/.../dns-ip

  Only PlainIP servers can
  be used in dnsmasq server=
  directives (not DoH/system/blocked).
end note
@enduml
```

### DNS Server Types

| Type | Address Format | Dnsmasq Support |
|------|---------------|-----------------|
| PlainIP | `"8.8.8.8"`, `"2001:db8::1"` | Yes (`server=` directive) |
| DoH | `"https://dns.google/dns-query"` | No |
| System | `"system"` | No |
| Blocked | `"rcode://refused"` | No |

### Generated Dnsmasq Config Example

```
# Generated by keen-pbr3 - do not edit manually

# List: my-domains
ipset=/example.com/example.org/my-domains
server=/example.com/example.org/10.8.0.1
```

### Domain Matching in DnsRouter

1. For each DNS rule (config order, first match wins):
   - Get the list's parsed domains
   - For each domain in the list:
     - Exact match: `domain == query`
     - Wildcard match: `*.example.com` matches `sub.example.com` AND `example.com` itself
2. If no rule matches → use fallback server

---

## REST API

Compiled only when `with_api` Meson option is `true` (default). Guarded by `#ifdef WITH_API`.

### Architecture

- **cpp-httplib** server runs in a background `std::thread`
- `Server::listen()` is blocking; `Server::stop()` is thread-safe
- Pimpl pattern hides httplib.h from the header
- `ApiContext` holds non-owning references to subsystems

### Endpoints

#### `GET /api/status`

Returns daemon status, version, active outbounds, and loaded list statistics.

```json
{
  "version": "3.0.0",
  "status": "running",
  "outbounds": [
    { "tag": "vpn", "type": "interface", "interface": "tun0", "gateway": "10.8.0.1", "ping_target": "8.8.8.8" },
    { "tag": "block", "type": "blackhole" }
  ],
  "lists": {
    "my-domains": { "ips": 0, "cidrs": 0, "domains": 5 },
    "my-ips": { "ips": 1, "cidrs": 1, "domains": 0 }
  }
}
```

#### `POST /api/reload`

Triggers immediate re-download and refresh of all lists (same as SIGUSR1).

```json
{ "status": "ok", "message": "Reload triggered" }
```

#### `GET /api/health`

Returns health check status for all configured outbounds.

```json
{
  "outbounds": [
    { "tag": "vpn", "type": "interface", "monitored": true, "status": "healthy" },
    { "tag": "wan", "type": "interface", "monitored": false, "status": "healthy" },
    { "tag": "block", "type": "blackhole", "monitored": false, "status": "healthy" }
  ]
}
```

Unmonitored outbounds (no `ping_target`) always report as `"healthy"`.

---

## Shutdown Sequence

```plantuml
@startuml shutdown
!theme plain
skinparam backgroundColor white

start
:SIGTERM or SIGINT received;
:daemon.run() returns\n(running_ = false);

if (API server running?) then (yes)
  :api_server→stop();
  :Join listen thread;
endif

:scheduler.cancel_all()\n(close all timerfds);

:route_table.clear()\n(remove routes in reverse order\nvia netlink);

:policy_rules.clear()\n(remove ip rules in reverse order\nvia netlink);

:firewall→cleanup();
note right
  iptables: delete mark rules
  (reverse order), then
  flush + destroy ipsets

  nftables: delete entire
  "inet keen_pbr3" table
end note

:remove_pid_file();

stop
@enduml
```

**Shutdown order** (dependencies require this sequence):
1. **API server** — stop accepting requests
2. **Scheduler** — cancel all timers, close timerfds
3. **RouteTable** — remove routes (reverse order via netlink)
4. **PolicyRuleManager** — remove ip rules (reverse order via netlink)
5. **Firewall** — remove mark rules, then ipsets
6. **PID file** — filesystem cleanup

---

## Build System

```plantuml
@startuml build
!theme plain
skinparam backgroundColor white

package "Build Pipeline" {
  [conanfile.py\nDependency recipe] as conan
  [meson.build\nBuild definition] as meson
  [meson_options.txt\nBuild options] as opts
}

package "Dependencies (Conan)" {
  [libcurl/8.5.0] as curl
  [nlohmann_json/3.11.3] as json
  [libnl/3.8.0] as libnl
  [mbedtls/3.5.0] as mbedtls
  [cpp-httplib/0.14.0\n(optional)] as httplib
}

package "Generators" {
  [PkgConfigDeps\n→ *.pc files] as pkgconfig
  [MesonToolchain\n→ conan_meson_native.ini] as toolchain
}

conan --> pkgconfig
conan --> toolchain
conan --> curl
conan --> json
conan --> libnl
conan --> mbedtls
conan --> httplib

meson --> pkgconfig : reads .pc files
meson --> toolchain : reads native file
opts --> meson
@enduml
```

### Meson Build Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_api` | boolean | `true` | Include REST API (cpp-httplib) |
| `firewall_backend` | combo | `auto` | `auto`, `iptables`, or `nftables` |

### Compiler Flags

- Standard: C++20
- Optimization: `-Os` (size), `-ffunction-sections`, `-fdata-sections`
- Linker: `-Wl,--gc-sections` (dead code elimination)
- LTO: enabled (`b_lto=true`)
- Cross-builds: `-static` (static linking for embedded deployment)
- API flag: `-DWITH_API` (when `with_api` is true)

### Source Files (22 total)

**Core (20 files):**
```
src/main.cpp
src/config/config.cpp
src/config/list_parser.cpp
src/http/http_client.cpp
src/lists/ipset.cpp
src/lists/list_manager.cpp
src/routing/target.cpp
src/routing/netlink.cpp
src/routing/route_table.cpp
src/routing/policy_rule.cpp
src/health/health_checker.cpp
src/health/circuit_breaker.cpp
src/firewall/firewall.cpp
src/firewall/iptables.cpp
src/firewall/nftables.cpp
src/dns/dns_server.cpp
src/dns/dns_router.cpp
src/dns/dnsmasq_gen.cpp
src/daemon/daemon.cpp
src/daemon/scheduler.cpp
```

**Conditional API (2 files, when `with_api` is true):**
```
src/api/server.cpp
src/api/handlers.cpp
```

---

## Cross-Compilation

### Supported Architectures

| Architecture | Conan Profile | Meson Cross-File | Toolchain Prefix |
|-------------|--------------|-------------------|-----------------|
| MIPS big-endian (OpenWRT) | `mips-be-openwrt` | `mips-be-openwrt.ini` | `mips-openwrt-linux-musl-` |
| MIPS little-endian (OpenWRT) | `mips-le-openwrt` | `mips-le-openwrt.ini` | `mipsel-openwrt-linux-musl-` |
| ARM (OpenWRT) | `arm-openwrt` | `arm-openwrt.ini` | `arm-openwrt-linux-muslgnueabihf-` |
| AArch64 (OpenWRT) | `aarch64-openwrt` | `aarch64-openwrt.ini` | `aarch64-openwrt-linux-musl-` |
| x86_64 (OpenWRT) | `x86_64-openwrt` | `x86_64-openwrt.ini` | `x86_64-openwrt-linux-musl-` |
| MIPS LE (Keenetic) | `mips-le-keenetic` | `mips-le-keenetic.ini` | `mipsel-linux-musl-` |

All profiles use musl libc and include `base-embedded` common settings.

### Build Commands

```bash
# Local build (native)
conan install . --build=missing
meson setup build
meson compile -C build

# Cross-build via Docker
docker build -f docker/Dockerfile.openwrt -t keen-pbr3-builder .
docker run --rm -v "$PWD/dist:/src/dist" keen-pbr3-builder mips-le-openwrt

# Manual cross-build
conan install . --profile:host=conan/profiles/mips-le-openwrt \
                --profile:build=default --output-folder=build/mips-le-openwrt --build=missing
meson setup build/mips-le-openwrt . \
    --cross-file=meson/cross/mips-le-openwrt.ini \
    --native-file=build/mips-le-openwrt/conan_meson_native.ini
meson compile -C build/mips-le-openwrt
```

---

## CI/CD Pipeline

```plantuml
@startuml cicd
!theme plain
skinparam backgroundColor white

|GitHub Actions|

start
:Push to main / PR;

fork
  :Build mips-be-openwrt;
fork again
  :Build mips-le-openwrt;
fork again
  :Build arm-openwrt;
fork again
  :Build aarch64-openwrt;
fork again
  :Build x86_64-openwrt;
fork again
  :Build mips-le-keenetic;
end fork

partition "Each Architecture Job" {
  :Checkout code;
  :docker build -f docker/Dockerfile.openwrt\n-t keen-pbr3-builder .;
  :docker run --rm\n-v dist:/src/dist\nkeen-pbr3-builder <arch>;
  :Upload artifact\nkeen-pbr3-<arch>;
}

stop
@enduml
```

- **Trigger**: Push to `main` or pull request to `main`
- **Strategy**: Matrix build, `fail-fast: false` (one failure doesn't cancel others)
- **Artifacts**: `keen-pbr3-<arch>` binary per architecture
- **Docker image**: Built per-job (no caching between jobs)

---

## File Layout

```
keen-pbr3/
├── include/
│   └── keen-pbr3/
│       └── version.hpp              # Version macros (3.0.0)
├── src/
│   ├── main.cpp                     # Entry point, CLI, subsystem init
│   ├── config/
│   │   ├── config.hpp               # Config structs + parse_config()
│   │   ├── config.cpp               # JSON deserialization (nlohmann_json)
│   │   ├── list_parser.hpp          # ParsedList, ListParser
│   │   └── list_parser.cpp          # IP/domain classification
│   ├── http/
│   │   ├── http_client.hpp          # HttpClient, HttpError
│   │   └── http_client.cpp          # libcurl wrapper
│   ├── lists/
│   │   ├── ipset.hpp                # IpSet, IpTrie (binary trie)
│   │   ├── ipset.cpp                # Trie insert/contains
│   │   ├── list_manager.hpp         # ListManager
│   │   └── list_manager.cpp         # Download, cache, merge
│   ├── routing/
│   │   ├── target.hpp               # RoutingDecision, resolve_route_action()
│   │   ├── target.cpp               # Failover chain resolution
│   │   ├── netlink.hpp              # NetlinkManager, RouteSpec, RuleSpec
│   │   ├── netlink.cpp              # libnl3 route/rule operations
│   │   ├── route_table.hpp          # RouteTable (tracking)
│   │   ├── route_table.cpp          # Add/remove/clear routes
│   │   ├── policy_rule.hpp          # PolicyRuleManager (tracking)
│   │   └── policy_rule.cpp          # Add/remove/clear ip rules
│   ├── firewall/
│   │   ├── firewall.hpp             # Abstract Firewall, factory
│   │   ├── firewall.cpp             # Backend detection, create_firewall()
│   │   ├── iptables.hpp             # IptablesFirewall
│   │   ├── iptables.cpp             # ipset + iptables CLI
│   │   ├── nftables.hpp             # NftablesFirewall
│   │   └── nftables.cpp             # nft CLI
│   ├── health/
│   │   ├── health_checker.hpp       # HealthChecker, HealthResult
│   │   ├── health_checker.cpp       # ICMP ping (SOCK_DGRAM)
│   │   ├── circuit_breaker.hpp      # CircuitBreaker, CircuitState
│   │   └── circuit_breaker.cpp      # State machine
│   ├── dns/
│   │   ├── dns_server.hpp           # DnsServerType, DnsServerConfig
│   │   ├── dns_server.cpp           # Address type detection
│   │   ├── dns_router.hpp           # DnsRouter
│   │   ├── dns_router.cpp           # Domain → DNS server matching
│   │   ├── dnsmasq_gen.hpp          # DnsmasqGenerator
│   │   └── dnsmasq_gen.cpp          # ipset=/server= generation
│   ├── daemon/
│   │   ├── daemon.hpp               # Daemon (epoll + signalfd)
│   │   ├── daemon.cpp               # Event loop
│   │   ├── scheduler.hpp            # Scheduler (timerfd)
│   │   └── scheduler.cpp            # Repeating/oneshot timers
│   └── api/                         # (conditional: WITH_API)
│       ├── server.hpp               # ApiServer (pimpl)
│       ├── server.cpp               # cpp-httplib background thread
│       ├── handlers.hpp             # ApiContext, register_api_handlers()
│       └── handlers.cpp             # GET/POST endpoint handlers
├── conan/
│   └── profiles/
│       ├── base-embedded            # Shared embedded settings
│       ├── mips-be-openwrt          # MIPS big-endian
│       ├── mips-le-openwrt          # MIPS little-endian
│       ├── arm-openwrt              # ARM (armv7hf)
│       ├── aarch64-openwrt          # AArch64
│       ├── x86_64-openwrt           # x86_64
│       └── mips-le-keenetic         # Keenetic MIPS LE
├── meson/
│   └── cross/
│       ├── mips-be-openwrt.ini
│       ├── mips-le-openwrt.ini
│       ├── arm-openwrt.ini
│       ├── aarch64-openwrt.ini
│       ├── x86_64-openwrt.ini
│       └── mips-le-keenetic.ini
├── docker/
│   ├── Dockerfile.openwrt           # Build container (Ubuntu 22.04)
│   └── build.sh                     # Cross-build script
├── .github/
│   └── workflows/
│       └── build.yml                # CI matrix build
├── conanfile.py                     # Conan 2.x recipe
├── meson.build                      # Meson build definition
├── meson_options.txt                # Build options
├── config.example.json              # Example configuration
└── .gitignore
```

---

## External Commands Reference

Complete list of all shell commands the daemon may execute at runtime:

### Firewall Backend Detection

| Command | Purpose |
|---------|---------|
| `command -v nft >/dev/null 2>&1` | Check if nft is available |
| `command -v iptables >/dev/null 2>&1` | Check if iptables is available |

### iptables Backend

| Command | Purpose |
|---------|---------|
| `ipset create <name> hash:net family <inet\|inet6> [timeout <N>] -exist` | Create IP set |
| `ipset add <name> <entry> [timeout <N>] -exist` | Add entry to set |
| `ipset flush <name> 2>/dev/null` | Clear all entries |
| `ipset destroy <name> 2>/dev/null` | Delete set |
| `iptables -t mangle -A PREROUTING -m set --match-set <name> dst -j MARK --set-mark 0x<HEX>` | Create packet mark rule |
| `iptables -t mangle -D PREROUTING -m set --match-set <name> dst -j MARK --set-mark 0x<HEX> 2>/dev/null` | Delete mark rule |
| `ip6tables -t mangle -A PREROUTING -m set --match-set <name> dst -j MARK --set-mark 0x<HEX>` | IPv6 mark rule |
| `ip6tables -t mangle -D PREROUTING -m set --match-set <name> dst -j MARK --set-mark 0x<HEX> 2>/dev/null` | Delete IPv6 mark rule |

### nftables Backend

| Command | Purpose |
|---------|---------|
| `nft add table inet keen_pbr3` | Create dual-stack table |
| `nft add chain inet keen_pbr3 PREROUTING '{ type filter hook prerouting priority mangle; policy accept; }'` | Create prerouting chain |
| `nft add set inet keen_pbr3 <name> '{ type <ipv4_addr\|ipv6_addr>; flags <interval[, timeout]>; [timeout Ns;] }'` | Create set |
| `nft add element inet keen_pbr3 <name> '{ <entry> [timeout Ns] }'` | Add element to set |
| `nft add rule inet keen_pbr3 PREROUTING ip daddr @<name> meta mark set 0x<HEX>` | Create mark rule |
| `nft flush set inet keen_pbr3 <name> 2>/dev/null` | Clear set |
| `nft delete set inet keen_pbr3 <name> 2>/dev/null` | Delete set |
| `nft -a list chain inet keen_pbr3 PREROUTING \| grep ... \| sed ...` | Find rule handle |
| `nft delete rule inet keen_pbr3 PREROUTING handle <N>` | Delete rule by handle |
| `nft delete table inet keen_pbr3 2>/dev/null` | Delete entire table (cleanup) |

### Netlink (kernel API, not shell)

These operations use the **libnl3** C library directly (not shell commands):

| Operation | Kernel Effect |
|-----------|--------------|
| `rtnl_route_add(NLM_F_CREATE\|NLM_F_REPLACE)` | `ip route add/replace default via <gw> dev <iface> table <N>` |
| `rtnl_route_delete()` | `ip route del default table <N>` |
| `rtnl_route_add(RTN_BLACKHOLE)` | `ip route add blackhole default table <N>` |
| `rtnl_rule_add(NLM_F_CREATE\|NLM_F_EXCL)` | `ip rule add fwmark 0x<HEX> table <N> priority <P>` |
| `rtnl_rule_delete()` | `ip rule del fwmark 0x<HEX> table <N>` |

---

## Packet Flow (End-to-End)

```plantuml
@startuml packetflow
!theme plain
skinparam backgroundColor white

start
:Incoming packet\n(PREROUTING);

partition "Firewall (mangle table)" {
  :Check packet dst IP\nagainst ipsets/nft sets;
  if (matches set?) then (yes)
    :Apply fwmark\n(0x10000, 0x10001, ...);
  else (no)
    :No mark applied\n(default routing);
    stop
  endif
}

partition "Policy Routing" {
  :Kernel checks ip rules\n(by priority);
  :Rule matches fwmark\n→ lookup table N;
}

partition "Route Lookup (table N)" {
  if (InterfaceOutbound?) then (yes)
    :Route via interface\n(e.g., tun0 via 10.8.0.1);
  elseif (TableOutbound?) then (yes)
    :Route via existing\ntable (e.g., table 200);
  elseif (BlackholeOutbound?) then (yes)
    :Packet dropped\n(blackhole route);
    stop
  endif
}

:Packet forwarded\nvia selected interface;
stop
@enduml
```

### Domain-Based Flow (via dnsmasq)

```plantuml
@startuml dnsflow
!theme plain
skinparam backgroundColor white

actor Client
participant "dnsmasq" as dnsmasq
participant "DNS Server" as dns
database "Kernel ipset/nft set" as ipset
participant "keen-pbr3\nfirewall rules" as fw

Client -> dnsmasq : DNS query\n(example.com)
dnsmasq -> dns : Forward query\n(server=/example.com/10.8.0.1)
dns -> dnsmasq : Response\n(93.184.216.34)
dnsmasq -> ipset : Add resolved IP\n(ipset=/example.com/my-list)
dnsmasq -> Client : DNS response

... later ...

Client -> fw : Traffic to 93.184.216.34
fw -> ipset : Check dst IP
ipset -> fw : Match found in my-list
fw -> fw : Apply fwmark 0x10000
fw -> Client : Route via configured outbound
@enduml
```
