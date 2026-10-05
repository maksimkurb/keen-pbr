# keen-pbr firewall architecture

How keen-pbr turns configuration into kernel firewall rules and how it checks
that the kernel still matches. Read this before changing anything in
`src/firewall/`.

keen-pbr is a helper daemon on embedded routers (Keenetic, OpenWrt). Every
design choice below favours **predictability and low RAM/CPU** over
cleverness.

## Pipeline

```
 Config ──► policy modules ──► FirewallPlan ──► lower_firewall_plan() ──► PhysicalRuleset (expected)
            (src/firewall/rules)  (logical intent)  (firewall_lowering.cpp)        │
                                                                                   ├──► backend renderer ──► kernel
                                                                                   │    (iptables.cpp / nftables.cpp)
                                                                                   ▼
 kernel dump ──► parse_iptables_save() / parse_nft_json() ──► PhysicalRuleset (observed)
                 (firewall_physical.cpp)                              │
                                                                      ▼
                                              verify_firewall_plan(): ordered per-chain diff
                                              (firewall_plan_verifier.cpp) ──► health / API
```

Each stage has exactly one job and knows nothing about the stages it does not
touch.

| Stage | Knows | Must NOT know |
|---|---|---|
| Policy module (`src/firewall/rules/*.cpp`) | *Why/what*: which traffic, which action, whether it is enabled | Backends, chain names, iptables/nft syntax |
| `FirewallPlan` (`firewall_plan.hpp`) | Desired logical rules + set declarations | Anything about a previous apply (physical names) |
| Lowering (`firewall_lowering.cpp`) | *How*: each logical action → ordered physical rules per chain, per backend | System state (it is a pure function) |
| Renderers (`iptables.cpp`, `nftables.cpp`) | Physical rule → text/JSON; lifecycle (sets, transactions, cleanup) | Policy actions (`MarkAction`, prefilters, …) |
| Parsers (`firewall_physical.cpp`) | Kernel text/JSON → `PhysicalRuleset` | Policy meaning — never guess "this is a restore rule" |
| Verifier (`firewall_plan_verifier.cpp`) | Compare two `PhysicalRuleset`s in order | Policy module ids, action types |

A dependency guard (`tests/check_firewall_dependencies.py`, run by
`make test`) fails the build if runtime, backends, verifier, snapshot, health
or daemon code includes `src/firewall/rules/*` or mentions a module id.

## Key types

- **`FirewallRuleInstance`** (`firewall_rule.hpp`): one logical rule — `key`
  (module id + instance id), `stage`, `priority`, `insertion_order`, `hook`,
  `family`, `criteria`, `action` (a `std::variant` of `MarkAction`,
  `BalanceAction`, `VerdictAction`, `RestoreConntrackMarkAction`,
  `SkipEstablishedOrDnatAction`, `SkipMarkedPacketsAction`,
  `InboundInterfaceFilterAction`, `SkipLanOutputAction`, `QueueAction`,
  `LogAction`).
- **`FirewallPlan`**: ordered rules, `sets` (`FirewallSetDeclaration`),
  `referenced_list_names`, `fwmark_mask`. Built by `build_firewall_plan()`
  (`firewall_runtime.cpp`) by running the module manifest.
- **`FirewallApplyResult`** (`firewall.hpp`): what the backend actually
  realized — apply mode, sorted physical set names, and the
  `expected_ruleset` (lowered chains + hook jumps) stored once per apply.
- **`ActiveFirewall`** (`src/routing/firewall_state.hpp`): `{plan, result,
  rule_states}`, published by the daemon as one
  `shared_ptr<const ActiveFirewall>` **only after a successful apply**.
  Readers take one snapshot and never see two applies mixed.
- **`PhysicalRuleset`** (`firewall_physical.hpp`): list of `PhysicalChain`s;
  each has a typed `PhysicalChainId` (role, table, family, setter
  mark — not a free-form name) and an ordered vector of `PhysicalRule`. A rule
  is `family` + ordered typed matches + ordered typed statements + optional
  `key` (diagnostics only) + `plan_rule` (index of the plan rule that produced
  it; ignored by `==`).

## Invariants (do not break these)

1. **Ownership is chain-scoped.** Every rule inside a keen-pbr chain
   (`KeenPbr*` iptables chains, the `inet KeenPbrTable` nft table, `setmark_*`
   setter chains) is ours, whatever its comment says. In system chains
   (PREROUTING, OUTPUT, …) only the jump rules into our chains are ours and are
   tracked. Foreign rules in system chains are never read into the ruleset and
   never touched.
2. **Comments are diagnostics, not truth.** Some routers have no `xt_comment`.
   Verification must work identically without comments (rules are attributed
   by position via `plan_rule`).
3. **Order matters.** One IP can be in several sets; the user's rule order
   decides which rule wins. Lowering preserves plan order (stage, priority,
   insertion order) inside each chain and the verifier checks order.
4. **One expansion.** A logical rule is expanded into physical rules in
   exactly one place: `lower_firewall_plan()`. Backends and the verifier never
   keep their own list of "what rule X should look like".
5. **No guessing from string shape.** Do not infer meaning from prefixes,
   substrings or characters (`find(':')` for IPv6, `rfind("kpbr4_")` for set
   kind, comment prefixes for rule kind). Carry a typed fact instead. Parsing
   *kernel output* is legitimate, but must be strict, centralised in one
   function per grammar, and reject near-misses.
6. **Unknown is never OK.** Anything a parser cannot translate becomes an
   explicit `UnknownMatch`/`UnknownStmt`, which never equals anything, so the
   verifier reports it instead of silently passing.
7. **Canonical form is shared.** Lowering and parsers both pass rules through
   `canonicalize_physical_rule()`. Add a canonicalization only for a kernel
   representation difference proven by a real fixture in
   `tests/firewall_it/fixtures/physical/`.
8. **Desired ≠ applied.** `FirewallPlan` never contains apply results. Apply
   results live in `FirewallApplyResult`.
9. **Failure keeps the previous active state.** No rollback is attempted; if
   the kernel was already changed the health verifier reports the drift.

## Backend lifecycle notes

- **iptables** has one classification chain per builtin hook and family, holding
  the lowered rules directly (no dispatchers, no A/B chains): PREROUTING
  classification in `KeenPbrRaw` (raw table, raw-prerouting mode) or
  `KeenPbrTable` (mangle), OUTPUT classification in `KeenPbrOutput` (mangle, in
  both modes). Placement is decided by lowering from the rule's hook: route
  rules (hook=prerouting) go to PREROUTING and, only when
  `iproute.process_router_traffic` is true (`FirewallPlan::process_router_traffic`,
  default false), also to OUTPUT; DNS-detour rules (hook=output) only to OUTPUT
  in both modes, and the inbound-interface prefilter (and its multi-interface
  fragments) only to PREROUTING, since router-originated packets have no input
  interface.
  **Router-originated traffic** (`iproute.process_router_traffic`): when false
  (default) OUTPUT holds only the prefilters and DNS-detour rules (no route
  mark/drop/pass/balance classifier, both backends), the output L7 sniff rules
  are not planned (no nft `sniff_out` chain; `KeenPbrSniff` is only jumped from
  FORWARD), and the DNS hold carries a `exclude_oif = {lo}` criterion lowered to
  `oifname != "lo"` / `! -o lo`, so replies of a local resolver to router-local
  processes (loopback) are not held or learned.  When true, router traffic is
  routed and learned like a LAN client.
  **Learning scope** (interception only): the client's interface is part of
  the interception criteria (`include_iif`/`exclude_iif` for the L7 sniff,
  `include_oif`/`exclude_oif` for the DNS hold, `ct_original` for the sniff),
  decided by the policy modules from `route.inbound_interfaces` (allowlist) or,
  when it is empty, from the outbound/WAN interfaces (all interface outbounds
  plus main-table default-route interfaces; denylist).  The forwarded L7 sniff
  rule is `iifname {allowed}` / `iifname != {wan...}` plus
  `ct direction original`; the output sniff copy has no input interface and
  keeps only `ct direction original`; the DNS hold is `oifname {allowed (+lo
  when router traffic is processed)}` / `oifname != {wan..., lo?}`.  nft emits
  one rule with an anonymous interface set.  iptables has no interface list:
  an allowlist is one fragment per interface (`-i br0 ...`, `-i br1 ...`), a
  denylist of one interface is `! -i wan`, a longer one leading
  `-i/-o <wan> -j RETURN` guard rules in the interception chain (no
  xt_comment needed); an output sniff copy already covered by a forward rule
  that only excludes interfaces is not emitted (router packets have no input
  interface, so they would be logged twice in the shared `KeenPbrSniff`).
  With an allowlist the forward rules need a positive `-i`, which an output
  copy cannot have, and the shared chain is jumped from FORWARD (an `-i`-less
  rule there would learn every interface).  OUTPUT then gets its own chain
  `KeenPbrSniffOut` (role `iptables_sniff_out`, pinned first jump from OUTPUT
  only, `KeenPbrSniff` is jumped from FORWARD only); a switch between layouts
  drops the stale jump and chain in the same restore.  With a denylist or no
  scope (or router traffic off) the single shared chain is kept.
  The WAN set belongs to the plan, so the inspector and verifier see it like
  any other rule, and a default-route change re-applies the firewall (the
  interface monitor refreshes on `default_route_changed` while interception is
  enabled with an empty allowlist).
  The `prefilter.skip_local_replies` prefilter is lowered to both PREROUTING
  and OUTPUT (`-m conntrack --ctdir REPLY -j RETURN` on iptables,
  `ct direction reply accept` in the nft `prerouting` and `output` chains),
  right after `restore_conntrack_mark` and ahead of the DNAT/marked-packet
  bypasses and all route classification.  In OUTPUT it keeps the answers local
  services (dnsmasq, uhttpd, sshd, the API) send to inbound connections on the
  main routing table.  In PREROUTING it keeps forwarded reply-direction packets
  (a WAN server answering a LAN client) from being re-marked by a catch-all
  rule when `route.inbound_interfaces` is empty (`restore_conntrack_mark` is
  ORIGINAL-only, `skip_established_or_dnat` skips DNAT only).  Balance
  (`numgen` / `statistic`, only unmarked packets) and the DNS-detour rules (hook=output) sit
  after it in stage order, so they never see reply-direction packets either;
  the interception chains are separate and unaffected.  Raw-mode limitation:
  raw PREROUTING runs before conntrack, so the rule is absent there (as are
  restore and the DNAT skip); in raw mode forwarded replies are protected only
  by `route.inbound_interfaces`.
  The `prefilter.skip_lan_output` prefilter (hook=output, OUTPUT only, never
  PREROUTING) comes right after `skip_local_replies` (same stage and priority,
  registered after it): `restore_conntrack_mark`, `skip_local_replies`,
  `skip_lan_output`, then the other bypasses.  Router-originated packets that
  start a new conntrack entry are not replies, yet some already leave through
  a LAN interface (a DHCP reply to a client without an address, RA/NDP, mDNS,
  SSDP, unicast to a LAN host); a catch-all rule such as
  `{"dest_addr":"0.0.0.0/0","proto":"udp"}` would re-mark and reroute them into
  a policy table.  The kernel picks the output device before mangle OUTPUT, so
  `oifname` there equals "the main table already sends it to the LAN".  Rules
  (keys `lan_oif`, `bcast`, `mcast`): (a) when `route.inbound_interfaces` is
  non-empty, `OifMatch` on those interfaces (nft `meta oifname { ... }`,
  iptables one `-o <if> -j RETURN` per interface); (b) always, destination
  address type broadcast and multicast (`AddrTypeMatch`: nft
  `fib daddr type broadcast|multicast`, iptables `-m addrtype --dst-type
  BROADCAST|MULTICAST`, one type per rule; the IPv6 iptables chain has no
  broadcast rule).  `OifMatch` is a physical match (canonicalized sorted and
  deduplicated, parsed back from `-o` and nft `oifname`) and lowering throws a
  `FirewallError` if one ever lands in a PREROUTING chain.
  A positive address match covering a whole family (`0.0.0.0/0`, `::/0`, or
  prefixes whose union is the family, which nft merges into `/0`) is dropped by
  canonicalization on both sides, because iptables-save omits it while nft
  prints it back; the rule keeps its family (`meta nfproto` guard on nft).
  Lowering does not split such a list per address on iptables.  A negated
  catch-all can never match and is rejected at lowering time.
  Apply is one `iptables-restore --noflush` transaction per table and family:
  it declares (flushes) our chains, appends the rules, ensures exactly one hook
  per chain in the builtin chains (the builtin chains are never declared, so
  foreign rules stay) and, in the same commit, flushes and deletes the retired
  A/B chains (`KeenPbrTable_A/B`, `KeenPbrRaw_A/B`, `KeenPbrOutput_A/B`,
  `KeenPbrTable_OUTPUT` and its `OUTPUT` hook) that exist and are no longer
  referenced. Leftover legacy chains classify as `other_owned` and are reported
  by the verifier until the next apply removes them.
  `Firewall::expected_hook_rules()` returns the expected builtin-chain jumps.
- **Interception** (`QueueAction` = DNS hold, `LogAction` = L7 sniff; hooks
  `postrouting` / `forward` + `output`; both groups are off unless
  `FirewallBuildContext::intercept` enables them) never enters the
  classification chains and ignores raw mode and the prefilters. iptables:
  mangle chains `KeenPbrDnsHold` (jumped from POSTROUTING) and `KeenPbrSniff`
  (one chain for FORWARD and OUTPUT, so the plan's forward and output copies of
  a sniff rule lower to one physical rule). These jumps are *pinned*: inserted
  with `-I <CHAIN> 1`, repaired by delete + re-insert, and removed (hook, flush,
  delete) as soon as the plan no longer carries the chain. The parser records
  the index of such a jump among ALL rules of the builtin chain
  (`PhysicalRule::hook_position`, foreign rules counted but never captured);
  the expected side carries 0, so a jump that is not first is plain drift.
  nftables: base chains `dns_hold` (postrouting), `sniff_fwd` (forward) and
  `sniff_out` (output), priority -150, present only when non-empty; no jumps.
- **nftables** replaces the whole `inet KeenPbrTable` content in one batch;
  balance uses `numgen inc mod N` + `vmap` into `setmark_XXXXXXXX` chains.
  Balancing also lowers on iptables (below); only `default_gateway` is nft-only.
- **iptables balance** needs no extra chain.  A `BalanceAction` with n >= 2
  usable candidates (per family: `FirewallBalanceCandidate::ipv4/ipv6`; zero
  usable is the fallback `MarkAction`, one is a plain `MarkAction`, exactly as
  on nft) becomes, for each expanded classifier match set M, in the same
  classification chain:
  `M -m mark --mark 0/<mask> -m statistic --mode random --probability p_i -j MARK --set-xmark c_i/<mask>`
  for i = 1..n-1 (the last without `statistic`), then `M -j CONNMARK
  --save-mark` and `M -j RETURN` like a `MarkAction`.  The mark guard is what
  ends the cascade once a candidate was chosen, and equals the nft rule's
  "only unmarked packets are balanced"; `p_i = 1/(n-i)` since candidates are
  equal (weight is only a group priority).  Stickiness is the existing
  restore/save machinery: `restore_conntrack_mark` restores the connection
  mark of ESTABLISHED flows and returns before any classifier, and the save
  rule stores the new choice, so the cascade only decides new connections.
  Without conntrack (raw PREROUTING) lowering throws, since every packet would
  be balanced again.  The probability is a `StatisticMatch` holding the kernel's
  `p * 2^31` fixed point (what xt_statistic stores; iptables-save prints it as
  `%.11f`, iptables-restore reads it back with `lround`), computed with integer
  arithmetic in lowering and parsed back from the decimal text, so the
  verifier compares integers.  Before the first restore `IptablesFirewall`
  runs an `iptables-restore --test` with a statistic rule and fails the apply
  with a message naming `xt_statistic` when the kernel cannot use it.
- Sets: logical and physical names are identical and stable: `kpbr4_<list>`,
  `kpbr6_<list>` (static) and `kpbr4d_<list>`, `kpbr6d_<list>` (dynamic).
  iptables apply order: sets, then rules, then cleanup. A missing static set is
  created directly and filled; an existing one is refreshed through a temp set
  (`kpbr4t_`/`kpbr6t_`): create + fill, `ipset swap temp final`, destroy temp,
  so the rules never see an empty set and capacity changes need no rule
  rewrite. A set failure aborts before any rule is restored. After the rules,
  owned static sets that are no longer referenced (removed lists, retired
  `kpbr4s_/kpbr4S_/kpbr6s_/kpbr6S_` generation sets) are destroyed; "in use"
  is logged, not fatal. Only names matching the strict owned grammar are ever
  destroyed. Dynamic sets (filled by interception) are never swapped.

## Apply modes

`Destructive` (recreate everything), `PreserveSets` (keep set contents,
rewrite rules), `RulesOnly` (rules only; the sets of the previous `FirewallApplyResult` must
exist under their stable names, otherwise exactly one fallback to
`PreserveSets`).

## Adding a policy

1. Add `src/firewall/rules/<name>.cpp` with a `constexpr kModuleId` and a
   `register_<name>_rules(context, registrar)` function; it decides from
   `FirewallBuildContext` whether it applies.
2. Add it to the manifest array in `firewall_rule_modules.cpp` (explicit
   order) and declare it in `firewall_rule_modules.hpp`.
3. If it only uses existing actions, nothing else changes. A **new action
   type** needs lowering support in `firewall_lowering.cpp` (both backends) and,
   if it uses a new kernel primitive, parser + renderer support.
4. Tests: module unit test, lowering test, round-trip against a real fixture,
   netns packet test (positive and negative).

## Testing

| What | Command | Notes |
|---|---|---|
| Build | `make` | |
| Unit tests + guards | `make test` | doctest, python unittests, dependency guard |
| Thread-safety | `make clang-check` | for changes touching shared state |
| netns integration | `make integration-tests-iptables INTEGRATION_CASES=a,b` / `make integration-tests-nftables INTEGRATION_CASES=a,b` | rootless; comma-separated case list |
| Real fixtures | `tests/firewall_it/scripts/capture_physical_fixtures.sh` | see `tests/firewall_it/fixtures/physical/README.md` |

Balance on iptables (`route_balance*` cases) needs `xt_statistic`; the preflight
reports it as the optional capability `balance_statistic`.  Probe bursts that
must hit both candidates use 24 source ports: the choice is random there
(missing one of two candidates has probability 2^-23).

Known issue: in the full nftables suite, every case after
`route_balance_failover` fails at `topology.sh reset` ("RTNETLINK answers: No
route to host"). This predates the refactor; run subsets until fixed.

## Resource rules

C++17, no new dependencies, no `std::regex`, no `std::istringstream` on
health/verification paths, reserve vectors, prefer enums and small structs,
strings only for names/addresses. Health checks run periodically: they read
only the needed tables (`nft -t` skips set elements) and do no re-lowering.
