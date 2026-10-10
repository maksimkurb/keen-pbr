# M0 results

> **Status:** OpenWrt aarch64 (router "router-kaz", OpenWrt 25.12.5, 1 GB RAM, dnsmasq 2.93):
> A, C1, D1a/b and E/E2 done. Earlier partial OpenWrt 2.93 rows are kept. Still pending: C2, B,
> Keenetic, Debian, and dnsmasq 2.92 on every platform. Cells marked "not run yet (needs hardware)"
> have no evidence. The dev-box rows at the bottom were captured on a non-router host and do
> not count as platform evidence.
>
> **M1 code currently uses the spec defaults** (until these results replace them):
> - preflight mode: conf-script (§10), not file mode
> - `PER_INSTANCE_RESTART=1` in the OpenWrt hook (§7.4)
> - `match_conf_file=/var/etc/dnsmasq.conf.<section>` (§7.4)
> - `kPreflightTimeout = 120 s` (§10.4)

| Exp | Platform | dnsmasq build | Result (paste the script line) | Decision |
|---|---|---|---|---|
| A1 | openwrt | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `A1 exit=0 marker=yes output=dnsmasq: syntax check OK.` | conf-script preflight mode confirmed on OpenWrt 2.93. |
| A1 | keenetic | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | keenetic | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | debian | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A1 | debian | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | openwrt | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `A2 exit=1 marker=no output= dnsmasq: bad option in output from .../bad.sh` | preflight detects bad directives. |
| A2 | keenetic | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | keenetic | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | debian | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A2 | debian | 2.93 no-ipset/nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A3 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `A3 exit=10 marker=no output= dnsmasq: .../fail.sh returns non-zero error code` | release note text: "If the conf-script exits non-zero, dnsmasq rejects its whole configuration (exit 10) and would not start. keen-pbr generate-resolver-config therefore always exits 0 and reports problems as a comment; only a crash of the generator could block dnsmasq." |
| A3 | openwrt | 2.92 ipset/no-nftset | not run yet (needs hardware) | not run yet (needs hardware) |
| A3 | keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | release note text: not run yet (needs hardware) |
| A4 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `A4 exit=0 marker=yes` | arguments in conf-script are supported. |
| A4 | openwrt 2.92 / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | not run yet (needs hardware) |
| A5 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `A5 baseline exit=0` | baseline OK (no decision). |
| A5 | openwrt 2.92 / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware) | not run yet (needs hardware) |
| B  | any | pre-2.86 (or conf-script rejecting) | not run yet (needs hardware) | not run yet (needs hardware) |
| C1 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `cmdline: /usr/sbin/dnsmasq -C /var/etc/dnsmasq.conf.cfg01411c -k -x /var/run/dnsmasq/dnsmasq.cfg01411c.pid; exe /usr/sbin/dnsmasq; pid 30778 ppid 30752 (30752 is the procd ujail wrapper; ubus reports the jail pid, not dnsmasq's); same netns as root; /proc/<pid>/fd readable as root; socket inodes 315718 = 127.0.0.1:53, 315710 = 192.168.55.1:53 found in /proc/net/udp; per-address sockets (bind-dynamic, no wildcard); sockets created after privilege drop belong to uid 453 (dnsmasq user) and are still in the fd list; runtime conf has conf-dir=/tmp/dnsmasq.cfg01411c.d (no filter); no dhcp helper child seen` | `match_conf_file=/var/etc/dnsmasq.conf.<section> confirmed; use the /proc scan, not the ubus pid; socket discovery works.` |
| C1 | keenetic | | not run yet (needs hardware): cmdline: ... | exe_required / default_conf_file: not run yet (needs hardware) |
| C1 | debian | | not run yet (needs hardware): cmdline: ... | not run yet (needs hardware) |
| C2 | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | not run (router has one dnsmasq section) | PER_INSTANCE_RESTART=1/0: not run (router has one dnsmasq section) |
| D1a | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `D1 exit=0 ms=40` with only 16 domains (3 rules, 3 lists); timing NOT representative (needs the large list set, target 450k domains) | preflight works on OpenWrt; keep kPreflightTimeout = 120 s until measured. It is unclear whether the drop-in was installed during this run (D1a vs D1b): rerun both. |
| D1a | keenetic | | not run yet (needs hardware): exit=.. ms=.. | kPreflightTimeout = .. s: not run yet (needs hardware) |
| D1a | debian | | not run yet (needs hardware): exit=.. ms=.. | kPreflightTimeout = .. s: not run yet (needs hardware) |
| D1b | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | not run yet (needs hardware) | not run yet (needs hardware); rerun, see D1a note |
| D1b | keenetic / debian | | not run yet (needs hardware) | not run yet (needs hardware) |
| E (10k/100k) | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | not run yet (needs hardware): start_ms, rss, hwm, timeouts, flip_s, E3 | M2 Go/No-Go: not run yet (needs hardware) |
| E (450k) | openwrt | 2.93 (OpenWrt package; ipset/nftset options not recorded) | `E start_ms~1010 rss_kb=4104 hwm_kb=4108`; after SIGHUP `rss_kb=11972 hwm_kb=11972 memavail_kb=624352`; E3 process alive=yes; m0-query.py: 0 answers, 100 % timeouts for the whole 60 s, including before SIGHUP | First run, not a valid measurement: the main test dnsmasq was CPU-bound loading the servers-file, and it never answered. Not a firewall problem. See "E (450k), first run: explanation" below. No Go/No-Go decision from this run. |
| A1–A5, C1 | openwrt aarch64 (router-kaz, OpenWrt 25.12.5, 1 GB RAM) | 2.93 | Same router as the "openwrt 2.93" A/C1 rows above (script output recorded there). | See those rows. |
| D1 generator (450k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | keen-pbr 3.4.0 generator: `2070 ms`, 9.5 MB output, 9447 `server=` lines. Throwaway config in /tmp with `daemon.max_file_size_bytes` raised to 64 MiB (default 8 MiB rejects a 450k list). | D1a proper (drop-in not installed) was not run: the drop-in is installed on this production router and was left untouched. |
| D1b (450k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | Real installed drop-in present, plus the candidate: `dnsmasq --test -C /var/etc/dnsmasq.conf.cfg01411c --conf-file=<candidate>` exit=0, 2180 ms and 2150 ms (two runs). Duplicate conf-script runs are accepted. | Duplicate-directive rule in §10.3 stays (it halves the work). |
| E2 servers (10k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | first answer 2320 ms (2280 ms CPU); SIGHUP reload flip 110 ms | See "E2: servers-file vs conf-file" below. |
| E2 servers (100k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | TIMEOUT: no answer within 150 s (150 s CPU, RSS 20.7 MB and still growing) | TIMEOUT. No-Go for servers-file at this size. |
| E2 servers (450k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | first run: still loading after 5 min (same as the E first run above) | No-Go. |
| E2 conf (10k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | first answer 90 ms | Same `server=` lines load fast via conf-file. |
| E2 conf (100k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | first answer 1060 ms, RSS 26.6 MB | Same as above. |
| E2 conf (450k) | openwrt aarch64 (router-kaz) | 2.93 (25.12.5) | first answer 4150 ms, 3580 ms CPU, RSS 111.7 MB | Same as above. |
| E (10k/100k/450k) | openwrt 2.92 / keenetic / debian | 2.92 / 2.93 | not run yet (needs hardware): start_ms, rss, hwm, timeouts, flip_s, E3 | M2 Go/No-Go: not run yet (needs hardware) |

Thresholds used for E: §3.6 table (unchanged). Record any changed values here before
M2 starts.

### E (450k), first run: explanation

The main test dnsmasq never answered. It was CPU-bound loading the servers-file. About
320 s later the process was still running in state R, with 315 s of CPU. Its socket
receive queue was full (5939 drops), and it ignored SIGTERM, so `kill -9` was needed.
This was not a firewall problem. The E table row above is therefore not a measurement.

### E2: servers-file vs conf-file

Same `server=` lines, loaded two ways (OpenWrt aarch64, dnsmasq 2.93):

- `--servers-file` (E2 servers): 10k domains, first answer 2320 ms (2280 ms CPU), SIGHUP
  reload flip 110 ms. 100k domains: no answer within 150 s (150 s CPU, RSS 20.7 MB and
  still growing), TIMEOUT. 450k domains: still loading after 5 min.
- `--conf-file` (E2 conf, the way M1 conf-script output is loaded): 10k 90 ms; 100k
  1060 ms (RSS 26.6 MB); 450k 4150 ms (3580 ms CPU, RSS 111.7 MB).

Loading through `--servers-file` is superlinear in dnsmasq 2.93: 10× the domains costs
more than 65× the time. At 100k the same lines load through conf-file in about 1 s.

### M2 decision: No-Go

M2 (SIGHUP reload of the servers-file) is cancelled. Reloading the servers-file is not
viable at keen-pbr list sizes. The threshold table above is not applicable. M2 could be
re-checked with fewer, larger lines or a newer dnsmasq.

### D1 at scale and kPreflightTimeout

- Generator for a 450k-domain list: 2070 ms, 9.5 MB output, 9447 `server=` lines.
  The temporary config needs `daemon.max_file_size_bytes` raised to 64 MiB. The default
  8 MiB rejects a 450k list.
- Preflight with the real installed drop-in present: exit=0, 2180 ms and 2150 ms (two runs).
- Rule `max(60 s, 3 × 2.2 s)` gives 60 s on this aarch64 router. Keep `kPreflightTimeout`
  at 120 s until a slow MIPS router (Keenetic) is measured, since it may be about 10× slower.

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
