# Agent Notes

## Build

Always build using the root `Makefile`:

```sh
make
```

This runs `cmake -S . -B cmake-build ...` followed by `cmake --build cmake-build`.

## C++

When a multiplication produces a size or count for a wider integer type, make
an operand the destination type so the multiplication itself is evaluated in
that type. For example, use `std::size_t{16} * 1024U`, not `16 * 1024` or a cast
of the completed product.

## Generated Files

Never edit generated files by hand. Update the source schema/config and run the
appropriate codegen command instead.

- Backend API types (`src/api/generated/api_types.hpp`): run `make generate`.
- Frontend API client/models (`frontend/src/api/generated/`): run `make frontend-api-generate`.

## Frontend

Frontend is lives in the `frontend/` folder. 
Always use bun/bunx as a package manager.
We are using base-ui instead of radix-ui.

Do not run make to compile C++ code if it wasn't edited (e.g. you edited only frontend code or docs)

@RTK.md

## Codex C++ Systems Team

The root agent is the sole Sol orchestrator. It owns requirements analysis,
architecture, planning, task decomposition, difficult decisions, delegation,
and final synthesis. Never spawn a Sol subagent. Unless a named agent is
appropriate, use the `coder` agent; do not create ad-hoc agents with an
unspecified model.

When spawning any subagent, set `fork_turns = "none"` explicitly. Supply only
the context needed for the assignment; subagents must not inherit the parent
conversation automatically.

For normal C++ fixes, features, refactors, and tests, prefer this workflow:

Sol -> one `coder` -> Sol

First, Sol makes a concrete plan and delegates the complete plan to one coder.
The coder owns repository investigation, call/data/state-flow tracing,
implementation, focused test updates, build/test iteration, and final-diff
inspection. Sol should pass the user goal, constraints, architectural context,
plan, and success criteria, but must not micromanage file-by-file exploration.
Do not split one coherent code change across agents or automatically add a
researcher/reviewer.

Use `researcher` only for an external technical question or a strictly
read-only investigation that materially affects the approach. Use `reviewer`
only for an independent review of a complex, risky, security-sensitive,
concurrency-sensitive, lifecycle/config-transaction, routing/firewall, or
regression-prone change. A reviewer reports ranked, concrete findings; the
same coder normally implements any follow-up fixes.

### C++ Systems Constraints

Before code changes, inspect the applicable AGENTS.md, RTK.md, Makefile,
CMakeLists.txt, tests, and relevant callers/callees. Keep C++17 and existing
dependencies/abstractions. Use the root Makefile for C++ verification and
prefix shell commands with `rtk`.

For configuration, reload/apply, lifecycle, and runtime-state changes, reason
explicitly about persisted, staged, active, desired-runtime, actual-system,
API-visible, and rollback/failure state. Trace request acceptance, mutation,
persistence, reconciliation, completion/failure notification, and event/API
publication. Prefer explicit ownership and transactional boundaries over
loosely synchronized state copies.

For routing/firewall work, do not assume iptables and nftables parity. Consider
fwmark/connmark masks, chain ordering, batching/atomicity, policy-rule
priorities, owned-versus-system state, IPv4/IPv6, interface churn, partial
failures, kernel capabilities, and target portability. Never deploy to or
modify real router or development-machine networking state unless explicitly
requested.

Choose the smallest sufficient checks, but use relevant integration coverage
for routing, firewall, lifecycle, or state behavior; compilation alone is not
enough. For concurrency-sensitive work, consider `make clang-check`. Report
environmental verification limits plainly. Do not hand-edit generated API
artifacts; regenerate them through the documented Makefile targets.
