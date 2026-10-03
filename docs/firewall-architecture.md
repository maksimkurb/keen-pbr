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
  `InboundInterfaceFilterAction`, `QueueAction`, `LogAction`).
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
  rules (hook=prerouting) go to PREROUTING and OUTPUT, DNS-detour rules
  (hook=output) only to OUTPUT, and the inbound-interface prefilter (and its
  multi-interface fragments) only to PREROUTING, since router-originated
  packets have no input interface.
  The `prefilter.skip_local_replies` prefilter (hook=output) is lowered to
  OUTPUT only (`-m conntrack --ctdir REPLY -j RETURN` on iptables,
  `ct direction reply accept` in the nft `output` chain), ahead of the
  DNAT/marked-packet bypasses and all route classification: route rules apply
  to router-originated traffic, but the answers local services (dnsmasq,
  uhttpd, sshd, the API) send to inbound connections must follow the main
  routing table, not a policy table.  PREROUTING does not get it: forwarded
  replies are handled by `route.inbound_interfaces` (reply packets arrive on
  the WAN/tunnel interface, which the filter skips).  Known gap: when
  `inbound_interfaces` is empty, a forwarded REPLY-direction packet (e.g. a WAN
  server answering a LAN client) is not restored (`restore_conntrack_mark` is
  ORIGINAL-only) and can be re-marked by a catch-all rule; the analogous
  prerouting `ct direction reply` skip is a possible follow-up.
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
  Balancing is nft-only; iptables rejects it at lowering time.
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

Known issue: in the full nftables suite, every case after
`route_balance_failover` fails at `topology.sh reset` ("RTNETLINK answers: No
route to host"). This predates the refactor; run subsets until fixed.

## Resource rules

C++17, no new dependencies, no `std::regex`, no `std::istringstream` on
health/verification paths, reserve vectors, prefer enums and small structs,
strings only for names/addresses. Health checks run periodically: they read
only the needed tables (`nft -t` skips set elements) and do no re-lowering.
