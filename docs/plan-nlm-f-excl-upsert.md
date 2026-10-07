# Plan: single-request upsert for set writes (drop `NLM_F_EXCL` on the hot path)

## Problem

Intercept set writes are slow (30–50 ms per 1-element batch, see the
"slow set write" log lines). `WriterBase::write_once()`
(`src/netfilter/set_writer.cpp`) sends every `add()` as an **exclusive** pass
(`NLM_F_EXCL`) first. For an element that already exists the kernel answers
`EEXIST`, and a **second** pass (`refresh_subset()`) then extends the timeout.
For nft that second pass is in-place `NEWSETELEM` or, without kernel support,
DELETE+ADD (2 requests). The caller does not care whether the element existed:
it only wants "this element exists with at least this timeout".

## Goal

One non-exclusive request per element on the hot path: the kernel creates the
element or overwrites its timeout in a single round trip. No `EEXIST`, no
refresh pass, no classification of "added vs refreshed".

## Facts to rely on (already in the code)

- ipset: `IPSET_CMD_ADD` without `NLM_F_EXCL` is `ipset add -exist`; the kernel
  overwrites timeout or creates the element (comment above `IpsetWriter`).
- nft: `NEWSETELEM` with `NLM_F_CREATE` and without `NLM_F_EXCL` updates an
  existing element's expiration **only on kernels that support it**. The startup
  probe `nft_timeout_update` (`in_place_refresh`) already tells us. Without it,
  a non-exclusive add of an existing element is a silent no-op, so timeouts
  would not extend.
- `build_nft_newsetelem_impl(..., with_expiration=true)` already builds the
  in-place message (`build_nft_refresh_setelem`).

## Changes

1. **New write mode `Upsert`** in `WriterBase::Mode`
   (`src/netfilter/set_writer.cpp`), used by `add()`:
   - ipset: one non-exclusive pass (`run_pass(..., exclusive=false)`).
   - nft with `in_place_refresh == true`: one pass using `build_in_place`
     (`NEWSETELEM` with expiration, no EXCL).
   - nft with `in_place_refresh == false`: keep today's behaviour
     (exclusive add, then DELETE+ADD for `EEXIST`). Do not regress old kernels.
   - Elements with `timeout_s == 0` (permanent) keep the current path, same as
     `refresh_subset()` does for in-place.
2. **Result semantics.** `Upsert` returns `Added` for every success
   (`Refreshed` is no longer distinguishable). Check consumers of
   `SetAddResult::Refreshed` in `intercept_processor.cpp` (lines near 739, 900,
   1014, 1226) and counters in `set_writer.hpp::Metrics`; drop or fold the
   "refreshed" counter, and update the Prometheus metric/docs if it exposes it.
3. **Keep `add_new()` and `refresh()`** as they are. `add_new()` needs `Exists`
   (exclusive by design). `refresh()` already skips the exclusive pass.
4. **Batch failure handling.** nft batches are one transaction. In the new
   path a failed batch rolls back every element, so on any error resend the
   acked elements non-exclusively. This is the existing `resend_` logic, and
   it becomes simpler because the first pass is already non-exclusive.
5. **Retry in `add_chunk()`.** The retry currently re-runs `Mode::Full`
   (exclusive first). Switch it to `Upsert` as well.
6. **Do not change** route/rule `NLM_F_EXCL` in `src/routing/netlink.cpp`
   (those need real "already exists" detection).

## Tests (`tests/test_set_writer.cpp`)

- ipset: `add()` of an existing element emits exactly 1 request, flags without
  `NLM_F_EXCL`, result `Added`.
- nft, in-place supported: 1 `NEWSETELEM` with expiration, no EXCL, for both new
  and existing elements.
- nft, in-place unsupported: unchanged request sequence (exclusive, then
  DELETE+ADD on `EEXIST`).
- Permanent element (`timeout_s == 0`) still uses the old path.
- Batch error rolls back and resends acked elements.
- Transient error retry uses the upsert path.
- Update existing tests that assert `Refreshed`.

## Verify

1. `make test BUILD_JOBS=14` and the filtered `keen-pbr-tests` run for set_writer.
2. Headless (`WITH_API=OFF`) and clang thread-safety builds.
3. `make integration-tests-nftables` and `-iptables` (netns, rootless) to
   confirm timeouts still extend on real kernels.
4. On the router: compare "slow set write" log lines and the new set-write
   timing metrics (`total`, `send`, `remainder`) before and after. Expect the
   `EEXIST` second pass to disappear; if `send` time stays high, the cost is the
   netlink send itself, not the extra round trip.

## Commit plan

- Commit 1 (separate from the Prometheus commit): "perf(set_writer): upsert
  elements in one non-exclusive request".

## Risks

- Kernel without nft in-place expiration update: must fall back (probe result
  is cached once at start, per the "kernel checks once at start" rule; do not
  probe lazily).
- Losing the Added/Refreshed distinction affects stats and logs only.
