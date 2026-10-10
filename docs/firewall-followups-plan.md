# Firewall follow-ups plan

Status: planned, not started. Created 2026-10-03 on `feature/load-balancing`
after the architecture refactor described in
[firewall-architecture.md](firewall-architecture.md).

Order: **1 → 2 → 3**. Task 1 may be a correctness bug, so it goes first.
Auto-repair of firewall drift is explicitly postponed (health reports drift;
repair happens on SIGUSR1 or config apply).

Every task follows the project rules: read `docs/firewall-architecture.md`
first, no guessing from string shape, keep RAM/CPU low, prove every new test
fails when the behaviour it guards is broken.

---

## 1. Investigate the `route_balance_no_leak` flake

### What the case checks

`tests/integration/container/cases/route_balance.py`:

1. A balance urltest outbound `auto` spans `wan_direct` and `wan_pbr`;
   198.18.0.10 is routed to `auto`.
2. Both WAN hosts drop the health-check port 18080, so both candidates fail.
3. The case waits until `/api/runtime/outbounds` reports neither candidate
   active.
4. New client connections must fail entirely: no observation at either WAN,
   including the main-table default via `wan_direct`.

Observed: failed once in three full nftables runs at `assert_no_leak` (a probe
reached a WAN host); passed when run alone.

### Hypothesis (unverified)

Race between runtime status and firewall publication: the API reports "both
excluded" as soon as urltest state changes, but the nft ruleset without
candidates is applied slightly later. A probe in that window still matches the
old `numgen` vmap and leaks. If confirmed, this is a product issue: during a
candidate change, traffic can briefly leave through a WAN that was just marked
failed, which matters for the no-leak guarantee.

### Steps

1. Loop the case 20–30 times; on failure capture the nft ruleset, runtime
   status, daemon log timestamps and the leaked observation.
2. If the race is confirmed: make the runtime publish "excluded" only after the
   firewall apply succeeded, or make the test wait on routing health / firewall
   state — whichever matches the intended contract (decide with evidence).
3. If the cause is elsewhere (stale conntrack, test timing): fix the test.

### Acceptance

- Root cause stated with captured evidence.
- 30 consecutive passes of `route_balance_no_leak`, then a full nftables suite
  pass.
- If the product changed: a test that fails on the old ordering.

Size: small to investigate, fix unknown. Model: Sonnet.

---

## 2. R1: cut peak RAM while loading sets

### Today

- **iptables:** list entries become `add kpbr4_x <cidr> -exist` lines in a
  per-set `std::ostringstream` (`IpsetRestoreVisitor`). At apply each buffer is
  copied (`buf.str()`) and appended to one large `ipset restore` script, piped
  in one write. Peak: about 3 copies of every set's lines.
- **nftables:** entries are kept as `nlohmann::json` arrays (one heap JSON
  string node per entry, several times the text size) and serialized into the
  batch document.

Lists of 100k+ entries cost several to tens of MB on 128–256 MB routers.

### Plan

- **iptables:** open one `ipset restore -exist` process per apply and stream
  each entry into its pipe as it is parsed (`create`/`flush` lines, then
  elements into the target set — the temp set on refresh). Pipe failure aborts
  before swap and before rules, as today.
- **nftables:** stream `add element` commands in chunks through the existing
  `nft_batch_pipe` instead of building one JSON document. First verify that
  chunking keeps the atomicity nft gives today for the table replacement; if
  not, write elements into the single batch as plain text without JSON nodes.

### Acceptance

- Existing ordering tests still hold: fill and swap before rules, failure
  aborts the apply.
- New test measures peak allocation (`mallinfo2`, as in step 2c) for a large
  synthetic list and shows roughly one copy instead of three (iptables) and no
  per-entry JSON nodes (nft).
- Unit tests, `clang-check`, full netns suites on both backends pass.

Size: medium (iptables contained; nft needs care around the transaction).
Model: Sonnet.

---

## 3. Typed set references

### Today

Set names are built and recognised by string prefixes in many places:

- `"kpbr4_" + list` / `"kpbr4d_" + list`: `src/config/routing_state.cpp:785-788`,
  `src/firewall/firewall.hpp:179-184`,
  `src/firewall/rules/route_targets.cpp:14`.
- "Is it dynamic?" via `rfind("kpbr4d_")`: `src/firewall/nftables.cpp:205`,
  `src/firewall/iptables.cpp:222`.
- Ownership during cleanup via an `rfind` chain: `src/firewall/iptables.cpp:1418-1423`,
  next to a stricter grammar parser at `src/firewall/iptables.cpp:340`.

This violates invariant 5 (no guessing from string shape), and the grammar is
spread across 6+ files.

### Plan

- One type: `FirewallSetRef { std::string list; FirewallFamily family;
  FirewallSetKind kind; }` with kind `static | dynamic | temp`.
- One formatter `to_name()` and one strict kernel-name parser
  `parse_owned_set_name()` (absorbs the `iptables.cpp:340` logic).
- Plan set declarations and rule criteria carry the ref instead of a string;
  backends check `ref.kind == dynamic` instead of prefixes.
- The parser is used only where kernel output is read (cleanup, RulesOnly
  preflight).

### Acceptance

- Physical names byte-identical: golden tests for every kind × family.
- Near-miss foreign names (`kpbr4x_foo`, invalid characters, empty list name,
  over-length) are never treated as owned.
- `rtk grep` finds no `"kpbr4`/`"kpbr6` literals or `rfind("kpbr` outside the
  formatter/parser and tests.
- Unit tests, `clang-check`, full netns suites on both backends pass.

Size: medium (many small call-site and test changes). Model: Sonnet.
