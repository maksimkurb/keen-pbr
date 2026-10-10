# dnsmasq M0 experiments

Blocking experiments before any dnsmasq M1 code. The scripts never edit production
dnsmasq configuration. Experiments D and E start temporary dnsmasq processes on
unused ports and kill them on exit. Results go in `docs/dnsmasq-m0-results.md`.

Run every router-side experiment on each platform (OpenWrt, Keenetic/Entware, Debian)
and on both dnsmasq builds (2.92 `ipset/no-nftset`, 2.93 `no-ipset/nftset`).

Files:

- `m0-router.sh`: experiments A, B, C, D, E (router side, POSIX sh, run as root).
- `m0-query.py`: load generator for E (LAN PC, Python 3 stdlib).

Set `DNSMASQ=/path/to/dnsmasq` to pick a binary (default: `command -v dnsmasq`).

## A: does `--test` execute `conf-script`?

```sh
sh m0-router.sh A
```

Prints `A1`..`A4` (conf-script cases) and `A5` (baseline). Expected:

- A1 (valid script, emits `server=`, `txt-record=`, ...): `exit=0 marker=yes`.
- A2 (script emits an unknown option): `exit≠0`.
- A3 (script exits 3 after valid output): informational. Copy it into release notes.
- A4 (`/bin/sh <script>` form): informational, whether arguments work.
- A5 (no conf-script): `exit=0`.

Decision:

- A1 `exit=0 marker=yes` on every build: M1 preflight uses conf-script mode (§10).
- A1 fails on a platform: M1 uses file mode there (§10.6). Record which.
- A2 must be `exit≠0`. Otherwise preflight cannot detect bad directives. Escalate
  instead of shipping a preflight safety claim.

## B: capability probe on a build without `conf-script`

```sh
DNSMASQ=/path/to/old/dnsmasq sh m0-router.sh B
```

Needs a dnsmasq older than 2.86 (or one that rejects `conf-script`). Expected: A1
fails with `exit≠0` while A5 still passes. This validates the
`DNSMASQ_CONF_SCRIPT_UNSUPPORTED` vs `DNSMASQ_TEST_FAILED` split in §9. If no such
binary exists, write "not tested".

## C: identify the instance

```sh
sh m0-router.sh C
```

Dumps the running dnsmasq processes (pid, exe, ppid, cmdline, netns, socket
inodes), `/proc/net/udp*`, TCP listeners, the relevant lines of the known
config files, and the uci/ubus/systemd view when those tools exist.

Also by hand on OpenWrt with two dnsmasq sections (C2): note all dnsmasq PIDs, run
`/etc/init.d/dnsmasq restart <first-section>`, note the PIDs again.

Decision:

- C1 (OpenWrt): main cmdline must contain `-C /var/etc/dnsmasq.conf.<section>`.
  If yes, the hook uses `match_conf_file=/var/etc/dnsmasq.conf.<section>` (§7.4).
  If not, record the real form and change §7.4 before M1.
- C1 (all): a second process with the same cmdline (the dhcp-script helper)
  must have its `ppid` equal to the main process (validates §8.4 step 4).
- C1 (all): socket inodes from `/proc/<pid>/fd` must appear in `/proc/net/udp*`.
  If `/proc/<pid>/fd` is unreadable for the jailed process, M1 relies on
  `static_endpoint` and probing only. Record it.
- C2: only the named instance's PID changed → per-instance restart (default, §7.4).
  All PIDs changed → `PER_INSTANCE_RESTART=0` in the OpenWrt hook plus a release note.
- Keenetic: record the Entware dnsmasq `exe` and whether its cmdline has `-C`.
  `exe_required=/opt/sbin/dnsmasq` and `default_conf_file=/opt/etc/dnsmasq.conf`
  in the hook must match.

## D: preflight of the real configuration

Writes a candidate drop-in exactly as the hook would and tests the production
config plus that drop-in. No restart.

```sh
# OpenWrt:
sh m0-router.sh D /var/etc/dnsmasq.conf.cfg01411c
# Keenetic:
sh m0-router.sh D /opt/etc/dnsmasq.conf
# Debian:
sh m0-router.sh D /etc/dnsmasq.conf -7 /etc/dnsmasq.d,.dpkg-dist,.dpkg-old,.dpkg-new
```

Arguments after the main config are the extra argv seen in the real cmdline.
Set `KEEN_PBR_BIN` if `keen-pbr` is not on `PATH`.

Run D1 twice:

- (a) with the keen-pbr drop-in **not** installed;
- (b) with it installed (the conf-script then runs twice: once from the conf-dir,
  once from the candidate).

Use the largest real list set (target: 450k domains).

Decision:

- D1(a) `exit=0`: preflight is viable. Record `ms`. M1 preflight timeout is
  `max(60 s, 3 × measured)`, rounded up to 10 s (§10.4). Put it into
  `kPreflightTimeout`.
- D1(b) `exit≠0` (duplicate directives rejected): keep the §10.3 rule "do not add the
  candidate when the installed drop-in is already identical". If `exit=0`, the rule
  stays anyway (it halves the work).
- D1(a) fails for a reason unrelated to keen-pbr (for example missing lease files):
  record it. §10.3 argv filtering may need extending.

## E: `servers-file` + SIGHUP at scale (gate for M2 only)

Topology:

```
LAN PC (m0-query.py) ──► <LAN_IP>:5398  "main" test dnsmasq  (servers-file=<WORK>/servers.conf)
                                         │ server=/m0canary.test/127.0.0.1#5399  (generation A)
                                         │ server=/m0canary.test/127.0.0.1#5400  (generation B)
                                         ▼
                        127.0.0.1:5399 upstream A: address=/m0canary.test/198.51.100.1
                        127.0.0.1:5400 upstream B: address=/m0canary.test/198.51.100.2
```

Steps:

1. On the router, start the instance and generate the 10k, 100k or 450k domain set:
   ```sh
   sh m0-router.sh E <LAN_IP> 450000
   ```
   It prints `start_ms`, RSS and HWM, then waits for Enter.
2. On the LAN PC, start the load generator (each run covers one domain count):
   ```sh
   python3 m0-query.py --server <LAN_IP> --port 5398 --rate 100 --duration 60
   ```
3. Press Enter on the router. The script sends SIGHUP after regenerating the
   servers file for upstream B. It prints RSS and HWM after 10 s and MemAvailable.
4. The script then appends `server=/broken` and sends SIGHUP again (E3). It
   reports whether the process is still alive. Check the PC output for which
   answers come back.

Repeat for 10k, 100k and 450k domains on each build.

Record: start time, RSS and HWM before and after, MemAvailable after, seconds with
`timeout > 0` around the SIGHUP, the time from SIGHUP until 100 % of answers are
`198.51.100.2`, and E3 behaviour (all servers dropped, old set kept, or the process
dies).

Note: `--user=root` is used so dnsmasq can re-read the servers file after dropping
privileges. Production M2 must make the file readable by the dnsmasq user (§20).

Decision for M2 (provisional thresholds; the maintainer may change them, but must
write the new values down before M2 starts):

| Metric (450k domains, worst build) | Go if |
|---|---|
| SIGHUP → all answers from generation B | ≤ 3 s |
| Queries timing out in the SIGHUP window | ≤ 2 % of queries sent in the 5 s around SIGHUP |
| VmHWM after SIGHUP | ≤ 1.5 × VmRSS before SIGHUP |
| MemAvailable after SIGHUP | ≥ 15 % of MemTotal |
| E3 (invalid line) | process stays alive **and** the behaviour is documented |

All "Go" → M2 may start. Any "No-Go" → M2 is cancelled; write the reason in the
results file.
