# M0 results

> **Status:** template. Every result cell is "not run yet (needs hardware)" except the
> dev-box rows at the bottom, which were captured on a non-router host and do not
> count as platform evidence.
>
> **M1 code currently uses the spec defaults** (until these results replace them):
> - preflight mode: conf-script (§10), not file mode
> - `PER_INSTANCE_RESTART=1` in the OpenWrt hook (§7.4)
> - `match_conf_file=/var/etc/dnsmasq.conf.<section>` (§7.4)
> - `kPreflightTimeout = 120 s` (§10.4)

| Exp | Platform | dnsmasq build | Result (paste the script line) | Decision |
|---|---|---|---|---|
| A1 | openwrt | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | openwrt | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | keenetic | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | keenetic | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | debian | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | debian | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | openwrt | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | openwrt | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | keenetic | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | keenetic | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | debian | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | debian | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A3 | openwrt / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | release note text: not run yet (needs hardware) |
| A4 | openwrt / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | not run yet (needs hardware) |
| A5 | openwrt / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | not run yet (needs hardware) |
| B  | any | pre-2.86 (or conf-script rejecting) | not run yet (needs hardware) | not run yet (needs hardware) |
| C1 | openwrt | | not run yet (needs hardware): cmdline: ... | match_conf_file form: not run yet (needs hardware) |
| C1 | keenetic | | not run yet (needs hardware): cmdline: ... | exe_required / default_conf_file: not run yet (needs hardware) |
| C1 | debian | | not run yet (needs hardware): cmdline: ... | not run yet (needs hardware) |
| C2 | openwrt | | not run yet (needs hardware): pids before/after: ... | PER_INSTANCE_RESTART=1/0: not run yet (needs hardware) |
| D1a | openwrt | | not run yet (needs hardware): exit=.. ms=.. | kPreflightTimeout = .. s: not run yet (needs hardware) |
| D1a | keenetic | | not run yet (needs hardware): exit=.. ms=.. | kPreflightTimeout = .. s: not run yet (needs hardware) |
| D1a | debian | | not run yet (needs hardware): exit=.. ms=.. | kPreflightTimeout = .. s: not run yet (needs hardware) |
| D1b | openwrt / keenetic / debian | | not run yet (needs hardware) | not run yet (needs hardware) |
| E (10k/100k/450k) | openwrt / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware): start_ms, rss, hwm, timeouts, flip_s, E3 | M2 Go/No-Go: not run yet (needs hardware) |

Thresholds used for E: §3.6 table (unchanged). Record any changed values here before
M2 starts.

## Dev box sanity (not platform evidence)

Host: dev box (Arch/CachyOS x86_64, Linux 7.2.9-2-cachyos), `/usr/bin/dnsmasq`
version 2.93 (compile options include both `ipset` and `nftset`, so it matches neither M0 build exactly).
Run as the non-root user with `sh scripts/dnsmasq-m0/m0-router.sh A`.

```
dnsmasq=/usr/bin/dnsmasq version=Dnsmasq version 2.93  Copyright (c) 2000-2026 Simon Kelley
A1 exit=3 marker=no output= dnsmasq: cannot read /tmp/kpbr-m0.Dfy5Wq/a.conf: Отказано в доступе
A2 exit=3 marker=no output= dnsmasq: cannot read /tmp/kpbr-m0.Dfy5Wq/a.conf: Отказано в доступе
A3 exit=3 marker=no output= dnsmasq: cannot read /tmp/kpbr-m0.Dfy5Wq/a.conf: Отказано в доступе
A4 exit=3 marker=no output= dnsmasq: cannot read /tmp/kpbr-m0.Dfy5Wq/a.conf: Отказано в доступе
A5 baseline exit=3
```

Result: the script runs, but the dev-box dnsmasq cannot read any config file owned
by the non-root user (EACCES, also for a plain `--test --conf-file=` under `$HOME`,
and with `--user=<self>`). A1–A5 are therefore **inconclusive** on this host and
say nothing about conf-script support. Re-run as root or on a router.
