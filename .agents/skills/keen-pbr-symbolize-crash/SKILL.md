---
name: keen-pbr-symbolize-crash
description: Diagnose Keen PBR native crashes from KPBR-CRASH v1 or legacy raw logs. Use when a user has a Keen PBR SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, or terminate report and wants the exact historical commit, temporary worktree, matching OpenWrt or Keenetic GitHub debug artifact, readable stack trace, symbol/function, source file, source line, or an evidence-backed explanation of an unresolved address.
---

# Keen PBR Crash Symbolization

Resolve historical crash addresses without modifying the user's current worktree and without guessing ASLR bases.

## Prefer KPBR-CRASH v1

For a complete `KPBR-CRASH v1` report, require only the log. Read version, 14-digit build ID, commit, branch, platform, firmware version, architecture, and variant from its `meta` line. Run:

```bash
python3 .agents/skills/keen-pbr-symbolize-crash/scripts/analyze_crash.py \
  --repo . \
  --log-file /path/to/keen-pbr-crash.log
```

Use explicit CLI metadata only to analyze a legacy report or override a known incorrect field. Never override embedded metadata silently.

## Collect legacy inputs

Require or infer these values:

- version, for example `3.0.7`
- build ID in `YYYYmmddHHMMSS`; interpret it as the commit time in UTC
- source branch and repository channel, such as `main`, `stable`, or another branch name
- platform (`openwrt` or `keenetic`), firmware version, and package architecture/config
- package variant: default to `full`; accept `headless`
- the complete crash log, including `stack pcs:` and memory-map/module lines when available

Treat source branch and artifact channel as the same value unless the user distinguishes them.

## Run legacy deterministic preparation

From the repository root, run:

```bash
python3 .agents/skills/keen-pbr-symbolize-crash/scripts/analyze_crash.py \
  --repo . \
  --branch main \
  --channel main \
  --version 3.0.7 \
  --build 20260705144235 \
  --platform openwrt \
  --target-version 24.10.6 \
  --arch aarch64_cortex-a53 \
  --variant full \
  --log-file /path/to/crash.log
```

Add `--fetch` when the requested branch may be absent or stale. Fetch only that branch. Add `--debug-file PATH` to reuse an already downloaded artifact. Add `--maps-file PATH` when a separate `/proc/<pid>/maps` capture is available. The script leaves its temporary worktree and downloaded artifact in place and prints their paths for inspection and cleanup.

For OpenWrt, use the artifact layout:

```text
https://raw.githubusercontent.com/maksimkurb/keen-pbr-repository/main/
repository/<channel>/openwrt-debug/<openwrt>/
keen-pbr_<version>-<build>_openwrt_<openwrt>_<arch>_<variant>.debug
```

For Keenetic, use `repository/<channel>/keenetic-debug/<version>/<package-arch>/keen-pbr_<version>-<build>_keenetic_<config>_<variant>.debug`.

## Interpret addresses conservatively

- For `ET_EXEC`, symbolize a runtime address directly only when it falls inside a `PT_LOAD` virtual-address range from the debug ELF.
- For `ET_DYN` or a relocated module, normalize with a load bias derived from a captured memory map and matching ELF segment. Never subtract a guessed high-address prefix.
- Treat `frame kind=fault`, `pc`, `rip`, and `eip` as fault instructions. Treat `frame kind=return`, link registers, and legacy stack PCs as return addresses; use the architecture-specific call-site adjustment implemented by the analyzer.
- Trust the embedded commit only after verifying its timestamp matches the build ID and it is reachable from the embedded branch.
- If the PC maps to a shared library, require that exact library/build's debug ELF plus its memory mapping. The Keen PBR debug file cannot symbolize libc, libstdc++, libcurl, or another DSO.
- Distinguish `function-only` from `source-line`. A symbol table can identify a function while missing DWARF line coverage produces `??:0`.
- Inspect the resolved file in the temporary worktree at the historical commit. Do not cite the same path from the current checkout.

If CodeGraph is unavailable inside the temporary worktree, use the resolved historical file directly. Do not use a current-checkout CodeGraph index as evidence for an old line.

## Recognize incomplete crash-handler output

At commits whose handler calls `write_frame_list()` after printing registers, a log that stops after `registers (...)` and then prints plain `Segmentation fault` did not capture the promised backtrace. Report a likely secondary fault during signal-safe unwinding separately from the original fault. Do not call the secondary fault the original root cause without additional evidence.

## Report the result

Include:

1. exact commit SHA, commit time, subject, and requested branch;
2. temporary worktree and debug artifact paths;
3. artifact URL and ELF type/load ranges;
4. a numbered stack trace plus each raw address, mapping, proven normalization, function, file, and line;
5. the exact historical source statement and a short causal explanation when resolved;
6. confidence and missing evidence when unresolved.

Say explicitly when the available data cannot establish a source line. Request one of the following, as applicable: full `stack pcs:` output, `/proc/<pid>/maps`, module-plus-offset output, the matching shared-library debug file, or a rebuild with application DWARF line information.

After completing the investigation, remove the temporary worktree with `git worktree remove <path>` and delete only the script-created temporary directory when cleanup is safe and desired.
