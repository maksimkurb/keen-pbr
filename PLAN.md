# keen-pbr Feature Backlog

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
(e.g., DNS queries, curl) bypasses PREROUTING and is not subject to keen-pbr marking.
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
- `keen-pbr --config <path> status` exits 0 and prints a readable report.
- Output includes per-outbound routing table summary and ip rule listing.
- Output includes firewall chain/set status (present / missing).
- Works when the daemon is not running (reads kernel state directly).
- No dependency on the REST API.

---

## 9. Implement `detour` for DNS servers

**How it works:**
The `detour` field in `dns.servers` is parsed and stored but never acted upon. The
`generate-resolver-config` subcommand emits `server=<domain>/<ip>` directives that tell
dnsmasq *which IP* to query, but nothing ensures that DNS traffic to that IP actually
goes through the specified outbound interface. To make `detour` work, keen-pbr must
create firewall mark rules for each DNS server that has a `detour` tag, so that UDP/TCP
packets destined for `server.address:53` are marked with the fwmark of the named outbound
and thus routed through it.

**Acceptance criteria:**
- For each entry in `dns.servers` with a `detour` field, a firewall rule is created that
  marks packets destined for `<server.address>:53` (UDP + TCP) with the fwmark of the
  referenced outbound.
- Rules are applied in both iptables and nftables backends.
- Rules are torn down and rebuilt on `full_reload()`.
- Referencing an unknown outbound tag in `detour` is a config validation error.
- `make test` covers fwmark rule generation for DNS detour.
- Docs (`dns.md`) are updated to document the `detour` field as implemented.

---

## 10. DNS fallback server in `generate-resolver-config`

**How it works:**
The `dns.fallback` server tag is validated and stored in `DnsServerRegistry` but never
used in `DnsmasqGenerator::generate()`. No global `server=<ip>` directive is emitted,
so dnsmasq uses its own default upstream for domains not covered by any DNS rule — not
the one configured in keen-pbr. A bare `server=<fallback_ip>` line (without a domain
path) should be appended to the generated config so dnsmasq forwards unmatched queries
to the intended server.

**Acceptance criteria:**
- `generate()` emits a global `server=<fallback_server_ip>` line for the configured
  `dns.fallback` server.
- The fallback `server=` line appears after all per-domain directives.
- If `dns.fallback` is not set, no global `server=` line is emitted.
- `make test` covers fallback server output in generated config.
- Docs (`dns.md`) updated to describe fallback behaviour accurately.

---

## 11. Fix `resolver_config_hash` to hash the full generated config

**How it works:**
`compute_config_hash()` currently hashes only a string of `domain/ipset4/ipset6` tuples.
This means the hash does not change if, for example, a DNS rule's server IP changes or
the fallback server changes — only domain-to-ipset mappings affect it. The hash should
instead be computed over the complete dnsmasq config output (excluding the TXT record
line itself), so any change to the generated file is reflected in the hash.

Implementation: stream the full config to the hashing stream,
compute the MD5 on-the-go, producing config to stdout at the same time, then write the `txt-record=config-hash.keen.pbr,<hash>` line at the end of dnsmasq config as the new line.

**Acceptance criteria:**
- `resolver_config_hash` changes whenever any part of the generated dnsmasq config
  changes (server IPs, ipset names, domain lists, fallback, etc.).
- The TXT record line is excluded from the hashed content.
- The TXT record is emitted at the **end** of the generated output (after all directives).
- `compute_config_hash()` and `generate()` produce consistent hashes for the same input.
- `make test` covers hash consistency and TXT record placement.

---

## 12. Boot wait for WAN (`boot_timeout`)

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

---

## 13. Separate static and dynamic ipsets/nftsets per list

**How it works:**
Currently each list gets a single ipset/nftset pair (`kpbr4_<list>` / `kpbr6_<list>`)
used for both statically-defined IPs (from `ip_cidrs`, `file`, `url`) and dynamically
resolved IPs (added by dnsmasq at DNS resolution time via `ipset=` / `nftset=`
directives). These have different lifecycle requirements:

- **Static entries** come from list data loaded at startup/reload and must persist until
  the next reload. They should never expire automatically.
- **Dynamic entries** are populated by dnsmasq when a listed domain is resolved. If
  `ttl_ms` is set on the list, these entries should expire after that many milliseconds
  so stale resolved IPs don't continue to be routed after a DNS record changes.

The fix is to maintain **two separate set pairs per list**:

| Set name | Contents | TTL |
|---|---|---|
| `kpbr4_<list>` / `kpbr6_<list>` | Static IPs from list data | none (permanent) |
| `kpbr4d_<list>` / `kpbr6d_<list>` | IPs resolved by dnsmasq | `ttl_ms` (if set) |

The firewall MARK rule for the list must match on **both** sets (OR semantics). The
`ipset=` / `nftset=` directives in `generate-resolver-config` output must reference the
dynamic set names (`kpbr4d_*` / `kpbr6d_*`). The TTL is applied when creating the
dynamic ipset (`--timeout`) or nftset (`timeout`).

**Acceptance criteria:**
- Static IPs (from `ip_cidrs`, `url`, `file` entries) are added only to the static set and are never expired.
- dnsmasq `ipset=` / `nftset=` directives reference the dynamic set (`kpbr4d_*` / `kpbr6d_*`).
- When `ttl_ms` is set on a list, the dynamic ipset/nftset is created with that timeout; entries added by dnsmasq expire automatically.
- When `ttl_ms` is `0` (default), the dynamic set has no timeout.
- Firewall MARK rules match packets in either the static or dynamic set for the list.
- `make test` covers set naming, TTL configuration, and dual-set firewall rule generation for both backends.
- Docs (`lists.md`) updated to clarify the static/dynamic split and the effect of `ttl_ms`.

---

## 14. Change `src_addr` / `dest_addr` from array to string with port-style syntax

**How it works:**
Currently `src_addr` and `dest_addr` are arrays of CIDR strings where negation is
indicated per-entry with a `!` prefix. This design allows mixed negation (some entries
negated, some not) which silently produces broken firewall rules. Replace the field type
with a single string using the same syntax as port specs:

- `"192.168.1.0/24"` — match this subnet
- `"192.168.1.0/24,10.0.0.0/8"` — match either subnet (comma-separated list)
- `"!192.168.1.0/24"` — NOT this subnet (single `!` at the start negates the whole value)
- `"!192.168.1.0/24,10.0.0.0/8"` — NOT either subnet

This makes negation unambiguous by design — one `!` at the start applies to all CIDRs,
so mixed negation is impossible.

**Changes required:**
- Update `RouteRuleElement` in `docs/openapi.yaml` (and regenerate `api_types.hpp`) to
  change `src_addr` / `dest_addr` from `array of string` to `string`.
- Update `daemon.cpp` to parse the string: strip leading `!` to detect negation, then
  split on `,` to get individual CIDRs.
- Update `config.example.json` and all docs to use string syntax.
- Remove the old array-based negation stripping logic.

**Acceptance criteria:**
- `src_addr` and `dest_addr` accept a single string value.
- A leading `!` on the whole string sets the negation flag; individual CIDRs never carry `!`.
- Comma-separated CIDRs work for both positive and negated forms.
- Old array-of-strings configs produce a clear parse error.
- `make test` covers single CIDR, comma list, negated single, and negated list cases.
- `docs/openapi.yaml`, `config.example.json`, and all relevant docs pages updated.
