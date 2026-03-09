# keen-pbr3 Feature Backlog

Features sourced from OpenWrt PBR (openwrt-22.03) gap analysis. Ordered by priority.

---

## 1. Kill-switch during reload

**How it works:**
Before tearing down existing firewall rules during a reload (SIGHUP / POST /api/reload /
list autoupdate), insert a temporary DROP rule on the FORWARD chain that blocks all
LAN→WAN traffic. After the new rules are fully applied, remove the temporary block.
This prevents traffic from silently leaking through the wrong (or no) interface during
the brief window when the firewall has been cleared but not yet rebuilt.

**Acceptance criteria:**
- During `full_reload()`, all forwarded traffic is blocked for the duration of the teardown+rebuild cycle.
- The temporary block is always removed — even if `apply_firewall()` throws an exception.
- A config option (e.g., `daemon.secure_reload: true`, default `true`) controls the feature.
- Normal steady-state traffic is unaffected when no reload is happening.
- `make test` passes.

---

## 2. Port and protocol matching in route rules

**How it works:**
Extend `RouteRule` to support optional `proto`, `src_port`, and `dest_port` fields.
When present, the firewall rule matches only packets with the specified protocol (tcp/udp/tcp/udp)
and port(s). Ports can be single values, comma-separated lists, or ranges (`80`, `80,443`,
`8000-9000`). Works in combination with existing list-based IP/domain matching.

Example config:
```json
"route": {
  "rules": [
    { "list": ["my-domains"], "outbound": "vpn", "proto": "tcp", "dest_port": "443" }
  ]
}
```

**Acceptance criteria:**
- `proto` accepts `"tcp"`, `"udp"`, `"tcp/udp"` (or omit for any protocol).
- `dest_port` and `src_port` accept single port, comma-separated list, or `start-end` range.
- iptables backend: uses `-p tcp --dport` / `--sport` / `-m multiport`.
- nftables backend: uses `tcp dport` / `udp dport` in rule expression.
- Omitting port/proto fields produces the same behavior as today (match all).
- `make test` covers port+proto parsing and rule generation.

---

## 3. Source and destination address fields in route rules

**How it works:**
Extend `RouteRule` to support optional `src_addr` and `dest_addr` fields (IPv4/IPv6
CIDR). When present, the firewall rule additionally matches on source or destination
address. This allows routing all traffic from a specific subnet through VPN without
needing an explicit IP list.

Example:
```json
{ "src_addr": "192.168.10.0/24", "outbound": "vpn" }
```

**Acceptance criteria:**
- `src_addr` / `dest_addr` accept a single CIDR string or an array of CIDR strings.
- Combined with `list`: both conditions must match (AND semantics).
- Both firewall backends handle the extra match correctly.
- `make test` covers src/dest addr rule generation.

---

## 4. Source MAC address matching in route rules

**How it works:**
Extend `RouteRule` to support an optional `src_mac` field (single MAC address or list).
The firewall marks packets originating from the specified hardware address regardless of
the device's current IP. Useful for DHCP clients whose IP may change.

Example config:
```json
"route": {
  "rules": [
    { "src_mac": "aa:bb:cc:dd:ee:ff", "outbound": "vpn" }
  ]
}
```

**Acceptance criteria:**
- `src_mac` accepts a single MAC string or an array of MAC strings.
- iptables backend: uses `-m mac --mac-source`.
- nftables backend: uses `ether saddr` match.
- A rule with only `src_mac` (no `list`) routes all traffic from that device.
- `src_mac` can be combined with `list` (both conditions must match).
- `make test` covers MAC-based rule generation for both backends.

---

## 5. Negation in route rule match fields

**How it works:**
Allow `!` prefix on `src_addr`, `dest_addr`, `src_port`, and `dest_port` fields to mean
"all traffic NOT matching this value". Example: `"src_addr": "!192.168.1.0/24"`.
In iptables this maps to `! -s 192.168.1.0/24`; in nftables to `ip saddr != 192.168.1.0/24`.

**Acceptance criteria:**
- `src_addr`, `dest_addr`, `src_port`, and `dest_port` fields accept `!`-prefixed strings.
- Both iptables and nftables backends emit correct negated match syntax.
- Negation can be combined with `list`, `proto`, other match fields.
- `make test` covers negated rule generation for both backends.

---

## 6. Strict enforcement (unreachable route when gateway down)

**How it works:**
When `strict_enforcement` is enabled for an outbound, and the gateway/interface for that
outbound is not reachable, add an `ip route add unreachable` entry to the routing table
instead of leaving it empty. This causes traffic hitting that table to receive ICMP
unreachable immediately rather than falling through to the default route and leaking to
the wrong interface.

**Acceptance criteria:**
- Config option `strict_enforcement: true` at outbound level (or global in `daemon` section).
- On startup and after reload, if a gateway for an INTERFACE outbound cannot be confirmed
  reachable, an `unreachable` default route is installed in that outbound's routing table.
- When the gateway becomes reachable (detected via SIGUSR1 re-check), the unreachable
  route is replaced with the real default route.
- `make test` passes.

---

## 7. OUTPUT chain support for locally-generated traffic

**How it works:**
PREROUTING only intercepts forwarded packets. Traffic originating from the router itself
(e.g., DNS queries, curl) bypasses PREROUTING and is not subject to keen-pbr3 marking.
Adding equivalent rules to the OUTPUT chain lets the router's own traffic be
policy-routed through the configured outbounds.

**Acceptance criteria:**
- Config option on route rules: `"chain": "output"` (alongside existing `"prerouting"` default).
- When `chain: "output"` is set, the firewall mark rules are inserted into OUTPUT instead
  of PREROUTING.
- Both iptables and nftables backends support the OUTPUT chain.
- Existing behavior (PREROUTING) is unchanged when `chain` is omitted.
- `make test` covers OUTPUT chain rule generation for both backends.

---

## 8. CLI status / diagnostic command

**How it works:**
Add a `status` subcommand to the CLI. It reads the config, then queries the live kernel
state and prints a human-readable summary of:
- Each outbound: type, interface/table, fwmark, routing table contents
- IP policy rules (`ip rule list`)
- Firewall chain and ipset/nftset presence (entry counts)
- Overall health aligned with what `GET /api/health/routing` returns

Works without the REST API being enabled or the daemon running.

**Acceptance criteria:**
- `keen-pbr3 --config <path> status` exits 0 and prints a readable report.
- Output includes per-outbound routing table summary and ip rule listing.
- Output includes firewall chain/set status (present / missing).
- Works when the daemon is not running (reads kernel state directly).
- No dependency on the REST API.

---

## 9. Boot wait for WAN (`boot_timeout`)

**How it works:**
Add an optional `daemon.boot_timeout_s` config field (default: 0 = disabled). When set,
the daemon polls for the presence of a default route on each configured INTERFACE
outbound's interface before proceeding with startup. If the route does not appear within
the timeout, startup proceeds anyway with a warning.

**Acceptance criteria:**
- `daemon.boot_timeout_s: 30` causes the daemon to wait up to 30 seconds for each
  INTERFACE outbound's gateway/interface to become reachable before building routing tables.
- Progress is logged every few seconds.
- If the timeout expires, a warning is logged and startup continues normally.
- `daemon.boot_timeout_s: 0` (default) skips waiting entirely (current behavior).
- `make test` passes.
