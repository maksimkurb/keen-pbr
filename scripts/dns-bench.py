#!/usr/bin/env python3
"""Measure DNS interception (hold) loss of a running keen-pbr daemon.

Sends DNS queries from a LAN client and classifies EVERY answer the client got
against the daemon's /api/dns/test INTERCEPT event stream:

  held-ok         event exists, timed_out=false, errors=0, added+refreshed>0
  hold-timeout    event timed_out=true (answer released before the set write
                  finished, the first connection may leak).  Events with
                  late_write=true whose write then succeeded are counted in the
                  "hold-timeout (late write ok)" sub-count: the IP is in the set
                  and stale flows were reset, only the first packets were unrouted.
  set-error       event errors>0
  no-write        event exists but nothing was written (e.g. stale snapshot)
  bypass          client got an A answer, no event, no sequence gap
                  (queue overrun / fail-open / rule miss)
  unknown         no event, but the event stream had a gap around that time
  client-timeout  no answer within --timeout
  nodata          answer without A records (NXDOMAIN / empty): no event expected

After ALL rate steps (and their settle time) finished, the answer IPs are
looked up with POST /api/routing/test {"target": "<ip>"} (never during load) to
verify that they really are in the live kernel dynamic sets.  This can turn a
held-ok into:

  not-in-set      the IP is not in any kernel set ("(default)")
  wrong-set       the IP is in the set of a different outbound

Entries have a timeout, so on long runs an IP may expire between the write and
the verification; the report prints the oldest answer age at verification.
Use --no-verify to skip.  With --verify-ssh root@ROUTER the same verification
reads the dynamic sets (kpbr4d_<list>/kpbr6d_<list>) of the bench list over one
key-based ssh call (nft -j or ipset save) instead of calling the API.

Python 3 stdlib only.  See docs "Measuring DNS interception loss".
"""
import argparse
import ipaddress
import json
import os
import random
import select
import shlex
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

OUTCOMES = ["held-ok", "hold-timeout", "set-error", "no-write", "bypass",
            "unknown", "client-timeout", "nodata", "not-in-set", "wrong-set"]
# outcomes whose answers carried A records, i.e. IPs worth verifying in the kernel
VERIFY_OUTCOMES = ("held-ok", "hold-timeout", "set-error", "no-write", "bypass", "unknown")
RATE_WARN_FRACTION = 0.9

# ---------------------------------------------------------------- DNS wire


def encode_name(name):
    out = bytearray()
    for label in name.rstrip(".").split("."):
        raw = label.encode("ascii")
        if not raw or len(raw) > 63:
            raise ValueError("bad label in %r" % name)
        out.append(len(raw))
        out += raw
    out.append(0)
    return bytes(out)


def encode_query(name, qid, qtype=1):
    """Standard recursive query for `name` (RD=1, one question, class IN)."""
    return struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0) + encode_name(name) + \
        struct.pack(">HH", qtype, 1)


def _read_name(data, pos):
    """Returns (name, position after the name in the original stream)."""
    labels = []
    end = None
    hops = 0
    while True:
        if pos >= len(data):
            raise ValueError("truncated name")
        length = data[pos]
        if length & 0xC0 == 0xC0:
            if pos + 1 >= len(data):
                raise ValueError("truncated pointer")
            if end is None:
                end = pos + 2
            pos = ((length & 0x3F) << 8) | data[pos + 1]
            hops += 1
            if hops > 32:
                raise ValueError("pointer loop")
            continue
        if length == 0:
            pos += 1
            break
        pos += 1
        labels.append(data[pos:pos + length].decode("ascii", "replace"))
        pos += length
    return ".".join(labels), (end if end is not None else pos)


def decode_response(data):
    """Returns dict(id, rcode, qname, addresses[A strings]) or None if malformed."""
    try:
        if len(data) < 12:
            return None
        qid, flags, qd, an, _ns, _ar = struct.unpack(">HHHHHH", data[:12])
        if not flags & 0x8000:
            return None
        pos = 12
        qname = ""
        for _ in range(qd):
            qname, pos = _read_name(data, pos)
            pos += 4
        addrs = []
        for _ in range(an):
            _n, pos = _read_name(data, pos)
            rtype, _cls, _ttl, rdlen = struct.unpack(">HHIH", data[pos:pos + 10])
            pos += 10
            if rtype == 1 and rdlen == 4:
                addrs.append(socket.inet_ntoa(data[pos:pos + 4]))
            pos += rdlen
        return {"id": qid, "rcode": flags & 0xF, "qname": qname.lower(),
                "addresses": addrs}
    except (ValueError, struct.error):
        return None


# ---------------------------------------------------------------- helpers


def percentile(sorted_values, pct):
    """Nearest-rank percentile of an ascending list; None if empty."""
    if not sorted_values:
        return None
    if pct <= 0:
        return sorted_values[0]
    rank = int(-(-pct * len(sorted_values) // 100))  # ceil
    return sorted_values[min(max(rank, 1), len(sorted_values)) - 1]


def dist(values):
    s = sorted(values)
    return {"count": len(s), "p50": percentile(s, 50), "p95": percentile(s, 95),
            "p99": percentile(s, 99), "max": s[-1] if s else None}


def make_nip_names(count, zone, rng=None):
    """Unique names <rand8>-198-18-<a>-<b>.<zone>; they resolve (nip.io style)
    to 198.18.a.b, inside the benchmarking range 198.18.0.0/15."""
    rng = rng or random.Random()
    seen = set()
    names = []
    while len(names) < count:
        rand8 = "".join(rng.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(8))
        name = "%s-198-18-%d-%d.%s" % (rand8, rng.randrange(256), rng.randrange(256), zone)
        if name not in seen:
            seen.add(name)
            names.append(name)
    return names


def load_domains(path, count, rng=None):
    rng = rng or random.Random()
    with open(path) as f:
        domains = [ln.strip().lower() for ln in f
                   if ln.strip() and not ln.lstrip().startswith("#")]
    if not domains:
        raise SystemExit("no domains in %s" % path)
    out = []
    while len(out) < count:
        batch = domains[:]
        rng.shuffle(batch)
        out += batch
    return out[:count]


# ---------------------------------------------------------------- classification


def classify(queries, events, gaps, stream_closed_at=None, gap_slack=0.2):
    """Fill q["outcome"] for every query.

    queries: list of dict(name, t_send, t_recv|None, rtt|None, rcode, addresses)
    events:  list of dict(INTERCEPT event + "_arrival" monotonic time)
    gaps:    list of dict(from_seq, to_seq, "_arrival", "_prev_arrival")
             _prev_arrival = arrival of the last event before the gap (or None)
    stream_closed_at: monotonic time the event stream broke early, if it did.
    """
    by_name = {}
    for ev in sorted(events, key=lambda e: e["seq"]):
        if ev.get("source") == "dns":
            by_name.setdefault(ev["domain"].lower(), []).append(ev)
    used = {}
    windows = []
    for g in gaps:
        start = g.get("_prev_arrival")
        windows.append(((start if start is not None else float("-inf")) - gap_slack,
                        g["_arrival"] + gap_slack))
    if stream_closed_at is not None:
        windows.append((stream_closed_at - gap_slack, float("inf")))

    for q in sorted(queries, key=lambda x: x["t_send"]):
        q["event"] = None
        if q.get("t_recv") is None or q.get("late"):
            q["outcome"] = "client-timeout"
            continue
        if not q.get("addresses"):
            q["outcome"] = "nodata"
            continue
        name = q["name"].lower()
        idx = used.get(name, 0)
        evs = by_name.get(name, [])
        if idx < len(evs):
            ev = evs[idx]
            used[name] = idx + 1
            q["event"] = ev
            if ev.get("timed_out"):
                q["outcome"] = "hold-timeout"
            elif ev.get("errors", 0) > 0:
                q["outcome"] = "set-error"
            elif ev.get("added", 0) + ev.get("refreshed", 0) > 0:
                q["outcome"] = "held-ok"
            else:
                q["outcome"] = "no-write"
            continue
        covered = any(lo <= q["t_send"] <= hi or lo <= q["t_recv"] <= hi
                      for lo, hi in windows)
        q["outcome"] = "unknown" if covered else "bypass"
    return queries


def achieved_rates(records):
    """Achieved send rate and receive rate (queries/s) of one step.

    Rate = intervals / span, i.e. (n-1) / (last - first) over the send (receive)
    timestamps; None when fewer than two timestamps or a zero span."""
    def rate(stamps):
        stamps = sorted(stamps)
        if len(stamps) < 2 or stamps[-1] <= stamps[0]:
            return None
        return (len(stamps) - 1) / (stamps[-1] - stamps[0])
    return {"send_rate": rate([r["t_send"] for r in records
                               if r.get("t_send") is not None and not r.get("send_error")]),
            "recv_rate": rate([r["t_recv"] for r in records if r.get("t_recv") is not None])}


def rate_warning(requested, achieved):
    """Warning text when the client could not reach the requested rate."""
    if achieved is None or requested <= 0 or achieved >= RATE_WARN_FRACTION * requested:
        return None
    return ("client could not reach requested rate (achieved %.0f qps): "
            "the client, not keen-pbr, is the bottleneck" % achieved)


def summarize(queries, events):
    total = len(queries)
    counts = {o: 0 for o in OUTCOMES}
    for q in queries:
        counts[q["outcome"]] += 1
    pct = {o: (100.0 * counts[o] / total if total else 0.0) for o in OUTCOMES}
    rtts = [q["rtt"] * 1000.0 for q in queries if q.get("rtt") is not None and not q.get("late")]
    matched = [q["event"] for q in queries if q.get("event")]
    late_writes = sum(1 for e in matched if e.get("late_write"))
    late_ok = sum(1 for q in queries
                  if q["outcome"] == "hold-timeout" and q.get("event")
                  and q["event"].get("late_write")
                  and q["event"].get("errors", 0) == 0
                  and q["event"].get("added", 0) + q["event"].get("refreshed", 0) > 0)
    return {
        "total": total, "counts": counts, "percent": pct,
        "late_writes": late_writes, "hold_timeout_late_ok": late_ok,
        "rtt_ms": dist(rtts),
        "hold_us": dist([e.get("hold_us", 0) for e in matched]),
        "set_write_us": dist([e.get("set_write_us", 0) for e in matched]),
        "parse_us": dist([e.get("parse_us", 0) for e in matched]),
    }


def counter_deltas(before, after):
    """before/after: the `intercept` object of /api/health/service (or None)."""
    out = {}
    bc = (before or {}).get("counters") or {}
    ac = (after or {}).get("counters") or {}
    for key in ("dns_packets", "dns_matched", "dns_hold_timeouts", "dns_late_writes",
                "dns_late_write_errors", "set_write_slow", "set_errors",
                "queue_overruns", "dns_parse_errors"):
        if key in bc and key in ac:
            out[key] = ac[key] - bc[key]
    bk = (before or {}).get("kernel_queue")
    ak = (after or {}).get("kernel_queue")
    if bk and ak:
        for key in ("queue_dropped", "user_dropped", "id_sequence"):
            out["kernel_" + key] = ak[key] - bk[key]
        if "dns_packets" in out:
            out["kernel_unprocessed"] = out["kernel_id_sequence"] - out["dns_packets"]
    return out


# ---------------------------------------------------------------- HTTP / SSE


class Api:
    def __init__(self, base, token=None, timeout=10):
        self.base = base.rstrip("/")
        self.token = token
        self.timeout = timeout

    def _request(self, path, data=None, timeout=None):
        headers = {}
        body = None
        if data is not None:
            body = json.dumps(data).encode()
            headers["Content-Type"] = "application/json"
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        return urllib.request.Request(self.base + path, data=body, headers=headers)

    def get_json(self, path):
        with urllib.request.urlopen(self._request(path), timeout=self.timeout) as r:
            return json.loads(r.read().decode())

    def post_json(self, path, data, timeout=None):
        with urllib.request.urlopen(self._request(path, data),
                                    timeout=timeout or self.timeout) as r:
            return json.loads(r.read().decode())

    def login(self, password):
        req = self._request("/api/auth/login", {"password": password})
        with urllib.request.urlopen(req, timeout=self.timeout) as r:
            self.token = json.loads(r.read().decode())["token"]

    def open_stream(self, path):
        return urllib.request.urlopen(self._request(path), timeout=2)


class EventStream(threading.Thread):
    """Reads /api/dns/test; keeps INTERCEPT events and GAP notices."""

    def __init__(self, api):
        super().__init__(daemon=True)
        self.api = api
        self.events = []
        self.gaps = []
        self.hello = threading.Event()
        self.stop_flag = threading.Event()
        self.error = None
        self.closed_at = None   # set when the stream ended before stop()
        self._last_arrival = None
        self._last_seq = None

    def _handle(self, payload, now):
        try:
            obj = json.loads(payload)
        except ValueError:
            return
        kind = obj.get("type")
        if kind == "HELLO":
            self.hello.set()
        elif kind == "INTERCEPT":
            seq = obj.get("seq", 0)
            if self._last_seq is not None and seq > self._last_seq + 1:
                # sequence jump without a GAP notice: treat as implicit gap
                self.gaps.append({"from_seq": self._last_seq + 1, "to_seq": seq - 1,
                                  "_arrival": now, "_prev_arrival": self._last_arrival,
                                  "implicit": True})
            self._last_seq = seq
            obj["_arrival"] = now
            self._last_arrival = now
            self.events.append(obj)
        elif kind == "GAP":
            obj["_arrival"] = now
            obj["_prev_arrival"] = self._last_arrival
            self.gaps.append(obj)
            self._last_seq = max(self._last_seq or 0, obj.get("to_seq", 0))

    def run(self):
        try:
            # The API defaults to the one-shot marker view. The benchmark
            # needs the continuous observation stream explicitly.
            resp = self.api.open_stream("/api/dns/test?show=all")
        except (urllib.error.URLError, OSError) as exc:
            self.error = str(exc)
            self.hello.set()
            return
        buf = b""
        try:
            while not self.stop_flag.is_set():
                try:
                    chunk = resp.readline()
                except (socket.timeout, TimeoutError):
                    continue
                except (OSError, ValueError) as exc:
                    self.error = str(exc)
                    break
                if chunk == b"":
                    if not self.stop_flag.is_set():
                        self.error = "event stream closed by the daemon"
                    break
                buf = chunk.strip()
                if buf.startswith(b"data:"):
                    self._handle(buf[5:].strip().decode("utf-8", "replace"), time.monotonic())
        finally:
            if not self.stop_flag.is_set():
                self.closed_at = time.monotonic()
            try:
                resp.close()
            except OSError:
                pass
            self.hello.set()

    def stop(self):
        self.stop_flag.set()
        self.join(timeout=5)


# ---------------------------------------------------------------- load


def send_queries(sock, dest, names, rate, timeout, rng=None):
    """Rate-paced sender + receiver. Returns list of query records."""
    rng = rng or random.Random()
    records = [{"name": n.lower(), "t_send": None, "t_recv": None, "rtt": None,
                "rcode": None, "addresses": [], "late": False} for n in names]
    outstanding = {}
    state = {"sent_all": False, "last_send": None}

    def sender():
        t0 = time.monotonic()
        for i, name in enumerate(names):
            target = t0 + i / rate
            while True:
                delay = target - time.monotonic()
                if delay <= 0:
                    break
                time.sleep(delay if delay > 0.002 else 0)
            qid = rng.randrange(65536)
            while qid in outstanding:
                qid = rng.randrange(65536)
            outstanding[qid] = i
            now = time.monotonic()
            records[i]["t_send"] = now
            try:
                sock.sendto(encode_query(name, qid), dest)
            except OSError:
                records[i]["send_error"] = True
            state["last_send"] = now
        state["sent_all"] = True

    thread = threading.Thread(target=sender, daemon=True)
    thread.start()
    answered = 0
    while True:
        now = time.monotonic()
        if state["sent_all"] and (answered == len(records) or now > state["last_send"] + timeout):
            break
        ready, _, _ = select.select([sock], [], [], 0.05)
        if not ready:
            continue
        try:
            data, _addr = sock.recvfrom(4096)
        except OSError:
            continue
        t_recv = time.monotonic()
        msg = decode_response(data)
        if msg is None:
            continue
        idx = outstanding.pop(msg["id"], None)
        if idx is None:
            continue
        rec = records[idx]
        rec["t_recv"] = t_recv
        rec["rtt"] = t_recv - rec["t_send"]
        rec["rcode"] = msg["rcode"]
        rec["addresses"] = msg["addresses"]
        rec["late"] = rec["rtt"] > timeout
        answered += 1
    thread.join()
    return records


def run_step(api, dest, names, rate, args, health_fn=None):
    health_fn = health_fn or (lambda: api.get_json("/api/health/service").get("intercept"))
    before = health_fn()
    stream = EventStream(api)
    stream.start()
    stream.hello.wait(5)
    if stream.error:
        raise SystemExit("cannot open /api/dns/test: %s" % stream.error)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    try:
        records = send_queries(sock, dest, names, rate, args.timeout)
    finally:
        sock.close()
    time.sleep(args.settle)
    stream.stop()
    after = health_fn()
    classify(records, stream.events, stream.gaps, stream.closed_at)
    summary = summarize(records, stream.events)
    summary["rate"] = rate
    summary.update(achieved_rates(records))
    summary["rate_warning"] = rate_warning(rate, summary["send_rate"])
    summary["counter_deltas"] = counter_deltas(before, after)
    summary["gaps"] = [{"from_seq": g["from_seq"], "to_seq": g["to_seq"]} for g in stream.gaps]
    summary["stream_error"] = stream.error
    return summary, records, stream


# ---------------------------------------------------------------- preflight


def _domain_matches(pattern, name):
    pattern = pattern.lower().lstrip("*.").rstrip(".")
    name = name.lower().rstrip(".")
    return name == pattern or name.endswith("." + pattern)


def preflight(config, health, names_or_zone):
    """Returns (ok, messages). names_or_zone: list of probe names (use the zone
    itself for nip mode). Lists backed by url/file cannot be verified."""
    msgs = []
    intercept = (health or {}).get("intercept") or {}
    ok = True
    if not intercept.get("dns_hold_active"):
        ok = False
        msgs.append("intercept.dns_hold_active is false (reasons: %s)"
                    % ("; ".join(intercept.get("reasons", [])) or "none"))
    lists = config.get("lists") or {}
    rules = ((config.get("route") or {}).get("rules")) or []
    active = set()
    for rule in rules:
        if rule.get("enabled") is False:
            continue
        active.update(rule.get("list") or [])
    covered = set()
    unverifiable = False
    for lname in active:
        lst = lists.get(lname) or {}
        domains = lst.get("domains") or []
        for probe in names_or_zone:
            if any(_domain_matches(d, probe) for d in domains):
                covered.add(probe)
        if lst.get("url") or lst.get("file"):
            unverifiable = True
    missing = [n for n in names_or_zone if n not in covered]
    if missing and not unverifiable:
        ok = False
        msgs.append(SNIPPET.format(domain=missing[0]))
    elif missing:
        msgs.append("warning: cannot verify that %s is in a routed URL/file list" % missing[0])
    return ok, msgs


SNIPPET = """\
no enabled route rule references a list containing "{domain}".
Add to the keen-pbr config (this only routes 198.18.0.0/15 via that outbound):

  "lists": {{ "dns_bench": {{ "domains": ["{domain}"] }} }},
  "route": {{ "rules": [ {{ "list": ["dns_bench"], "outbound": "<any outbound tag>" }} ] }}

then apply the config and re-run."""


# ---------------------------------------------------------------- kernel set verification


def expected_outbound(config, probe):
    """(outbound, rule_index, list_name, candidates) of the first enabled route
    rule (in order) referencing a list that covers one of `probe` (domain
    patterns); outbound is None when it cannot be determined (URL/file lists)."""
    lists = config.get("lists") or {}
    rules = ((config.get("route") or {}).get("rules")) or []
    candidates = []
    for idx, rule in enumerate(rules):
        if rule.get("enabled") is False:
            continue
        for lname in rule.get("list") or []:
            domains = (lists.get(lname) or {}).get("domains") or []
            if any(_domain_matches(d, p) for d in domains for p in probe):
                candidates.append((rule.get("outbound"), idx, lname))
                break
    if not candidates:
        return None, None, None, []
    return candidates[0] + (candidates,)


def verdict_for(actual, expected):
    """in-set | wrong-set | not-in-set | unverifiable for one IP."""
    if actual is None or actual == "(unknown)":
        return "unverifiable"
    if actual == "(default)":
        return "not-in-set"
    if expected is None or actual == expected:
        return "in-set"
    return "wrong-set"


def check_ip(api, ip, expected):
    """Returns (verdict, actual_outbound|None)."""
    try:
        resp = api.post_json("/api/routing/test", {"target": ip})
        entries = resp.get("results") or []
        entry = next((e for e in entries if e.get("ip") == ip), entries[0] if entries else None)
        actual = entry.get("actual_outbound") if entry else None
    except (urllib.error.URLError, OSError, ValueError, AttributeError):
        return "unverifiable", None
    return verdict_for(actual, expected), actual


def collect_ips(records):
    """{ip: time of the first answer carrying it} for queries worth verifying."""
    out = {}
    for q in records:
        if q.get("outcome") in VERIFY_OUTCOMES:
            for ip in q.get("addresses") or []:
                if ip not in out or q["t_recv"] < out[ip]:
                    out[ip] = q["t_recv"]
    return out


def verify_sets(api, all_records, expected, rate, sleep=time.sleep, clock=time.monotonic):
    """Verify every unique answer IP, sequentially at <= `rate` req/s, and apply
    the final outcome overrides to `all_records` (flat list).  Must only be
    called after the last query of the last step."""
    ips = collect_ips(all_records)
    started = clock()
    interval = 1.0 / rate if rate and rate > 0 else 0.0
    verdicts, actuals, ages = {}, {}, {}
    for n, (ip, t_answer) in enumerate(sorted(ips.items(), key=lambda kv: kv[1])):
        due = started + n * interval
        if clock() < due:
            sleep(due - clock())
        verdicts[ip], actuals[ip] = check_ip(api, ip, expected)
        ages[ip] = clock() - t_answer
    duration = clock() - started

    return finalize_verification(all_records, ips, verdicts, ages, duration, expected)


def finalize_verification(all_records, ips, verdicts, ages, duration, expected, method="api"):
    """Apply the verdicts to `all_records` (outcome overrides) and build the
    verification summary shared by the API and the SSH method."""
    counts = {"in-set": 0, "not-in-set": 0, "wrong-set": 0, "unverifiable": 0}
    for v in verdicts.values():
        counts[v] += 1
    eventually_in_set = eventual_miss = verified_queries = 0
    for q in all_records:
        if q.get("outcome") not in VERIFY_OUTCOMES:
            continue
        qv = [verdicts[ip] for ip in q.get("addresses") or [] if ip in verdicts]
        if not qv:
            continue
        # worst address wins: the daemon must have written every answer IP
        for worst in ("not-in-set", "wrong-set", "unverifiable", "in-set"):
            if worst in qv:
                q["verified"] = worst
                break
        verified_queries += 1
        if q["verified"] == "not-in-set":
            eventual_miss += 1
        if q["outcome"] == "hold-timeout" and q["verified"] == "in-set":
            eventually_in_set += 1
        if q["outcome"] == "held-ok" and q["verified"] in ("not-in-set", "wrong-set"):
            q["outcome"] = q["verified"]
    return {
        "checked_ips": len(ips), "in_set": counts["in-set"],
        "not_in_set": counts["not-in-set"], "wrong_set": counts["wrong-set"],
        "unverifiable": counts["unverifiable"],
        "verified_queries": verified_queries, "eventually_in_set": eventually_in_set,
        "eventual_miss": eventual_miss,
        "eventual_miss_percent": 100.0 * eventual_miss / verified_queries if verified_queries else 0.0,
        "duration_s": duration, "max_age_s": max(ages.values()) if ages else 0.0,
        "expected_outbound": expected, "method": method,
        "bad_ips": sorted(ip for ip, v in verdicts.items() if v in ("not-in-set", "wrong-set"))[:20],
    }


# ---------------------------------------------------------------- kernel set verification via SSH


SSH_SCRIPT = """\
# POSIX sh (busybox ok): dump the dynamic kernel sets of the bench list(s).
if nft list table inet KeenPbrTable >/dev/null 2>&1; then
  echo "@@BACKEND nft"
  for s in {sets}; do
    echo "@@SET $s"
    nft -j list set inet KeenPbrTable "$s" 2>/dev/null || echo "@@MISSING $s"
  done
elif command -v ipset >/dev/null 2>&1; then
  echo "@@BACKEND ipset"
  for s in {sets}; do
    echo "@@SET $s"
    ipset save "$s" 2>/dev/null || echo "@@MISSING $s"
  done
else
  echo "@@BACKEND none"
fi
echo "@@END"
"""


def dynamic_set_names(lists):
    out = []
    for lname in lists:
        for prefix in ("kpbr4d_", "kpbr6d_"):
            if prefix + lname not in out:
                out.append(prefix + lname)
    return out


def ssh_command(args):
    cmd = shlex.split(args.ssh_bin) + ["-o", "BatchMode=yes", "-o", "ConnectTimeout=5"]
    for opt in args.ssh_opt or []:
        cmd += ["-o", opt]
    return cmd + [args.verify_ssh, "sh", "-s"]


class SshError(Exception):
    pass


def fetch_sets_over_ssh(args, set_names, run=subprocess.run):
    """ONE ssh call; returns (backend, {set_name: output text | None if missing})."""
    script = SSH_SCRIPT.format(sets=" ".join(shlex.quote(n) for n in set_names))
    try:
        proc = run(ssh_command(args), input=script, capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError) as exc:
        raise SshError("cannot run ssh: %s" % exc)
    if proc.returncode != 0:
        raise SshError("ssh failed (exit %d): %s" % (proc.returncode, proc.stderr.strip()))
    backend, sets, current, ended = None, {}, None, False
    for line in proc.stdout.splitlines():
        if line.startswith("@@BACKEND "):
            backend = line.split(None, 1)[1].strip()
        elif line.startswith("@@SET "):
            current = line.split(None, 1)[1].strip()
            sets[current] = []
        elif line.startswith("@@MISSING "):
            sets[line.split(None, 1)[1].strip()] = None
            current = None
        elif line.startswith("@@END"):
            ended = True
        elif current is not None and sets.get(current) is not None:
            sets[current].append(line)
    if not ended or backend is None:
        raise SshError("incomplete reply from the router (stderr: %s)" % proc.stderr.strip())
    if backend == "none":
        raise SshError("neither the nft table inet KeenPbrTable nor ipset was found on the router")
    return backend, {k: (None if v is None else "\n".join(v)) for k, v in sets.items()}


def _addr_range(text):
    """(version, lo, hi) of an address, CIDR prefix or 'a-b' range."""
    text = text.strip()
    if "-" in text:
        a, b = (ipaddress.ip_address(x.strip()) for x in text.split("-", 1))
        return a.version, int(a), int(b)
    net = ipaddress.ip_network(text, strict=False)
    return net.version, int(net.network_address), int(net.broadcast_address)


def _nft_value(val):
    if isinstance(val, str):
        return _addr_range(val)
    if isinstance(val, dict) and "prefix" in val:
        return _addr_range("%s/%s" % (val["prefix"]["addr"], val["prefix"]["len"]))
    if isinstance(val, dict) and "range" in val:
        a, b = (ipaddress.ip_address(x) for x in val["range"])
        return a.version, int(a), int(b)
    raise ValueError("unsupported nft element %r" % (val,))


def parse_nft_set(text):
    """[(version, lo, hi, expires_s|None)] from `nft -j list set` output."""
    out = []
    doc = json.loads(text)
    for item in doc.get("nftables", []):
        st = item.get("set")
        if not st:
            continue
        for el in st.get("elem") or []:
            expires = None
            if isinstance(el, dict) and "elem" in el:
                expires = el["elem"].get("expires")
                el = el["elem"]["val"]
            out.append(_nft_value(el) + (expires,))
    return out


def parse_ipset_save(text):
    """[(version, lo, hi, timeout_s|None)] from `ipset save <set>` output."""
    out = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[0] != "add":
            continue
        expires = None
        if "timeout" in parts[3:]:
            try:
                expires = int(parts[parts.index("timeout", 3) + 1])
            except (ValueError, IndexError):
                pass
        try:
            out.append(_addr_range(parts[2]) + (expires,))
        except ValueError:
            continue
    return out


class SetIndex:
    def __init__(self, entries):
        self.exact, self.ranges = {}, []
        for ver, lo, hi, exp in entries:
            if lo == hi:
                self.exact[(ver, lo)] = exp
            else:
                self.ranges.append((ver, lo, hi, exp))

    def lookup(self, ip):
        """(found, expires_s|None)"""
        addr = ipaddress.ip_address(ip)
        key = (addr.version, int(addr))
        if key in self.exact:
            return True, self.exact[key]
        for ver, lo, hi, exp in self.ranges:
            if ver == key[0] and lo <= key[1] <= hi:
                return True, exp
        return False, None


def verify_ssh(args, all_records, lists, run=subprocess.run, clock=time.monotonic):
    """Post-run verification through ONE ssh call.  Returns the same summary
    dict as verify_sets (method 'ssh nft' / 'ssh ipset')."""
    ips = collect_ips(all_records)
    names = dynamic_set_names(lists)
    started = clock()
    backend, raw = fetch_sets_over_ssh(args, names, run=run)
    parse = parse_nft_set if backend == "nft" else parse_ipset_save
    indexes = {}
    for name, text in raw.items():
        if text is not None:
            try:
                indexes[name] = SetIndex(parse(text))
            except (ValueError, KeyError, TypeError) as exc:
                raise SshError("cannot parse %s output for %s: %s" % (backend, name, exc))
    verdicts, ttls, ages = {}, [], {}
    now = clock()
    for ip, t_answer in ips.items():
        ver = ipaddress.ip_address(ip).version
        cands = [n for n in names if n.startswith("kpbr%dd_" % ver)]
        have = [indexes[n] for n in cands if n in indexes]
        if not have:
            verdicts[ip] = "unverifiable"  # set missing
        else:
            hits = [h for h in (ix.lookup(ip) for ix in have) if h[0]]
            if hits:
                verdicts[ip] = "in-set"
                exp = [e for _f, e in hits if e is not None]
                if exp:
                    ttls.append(max(exp))
            else:
                verdicts[ip] = "not-in-set"
        ages[ip] = now - t_answer
    v = finalize_verification(all_records, ips, verdicts, ages, clock() - started, None,
                              method="ssh " + backend)
    v["sets"] = names
    v["missing_sets"] = sorted(n for n, t in raw.items() if t is None)
    v["ttl_min_s"] = min(ttls) if ttls else None
    v["ttl_max_s"] = max(ttls) if ttls else None
    return v


def render_verification(v):
    lines = ["== Kernel set verification (after the run, method: %s) ==" % v.get("method", "api"),
             "  sets: %s" % ", ".join(v["sets"]) if v.get("sets")
             else "  expected outbound: %s" % (v["expected_outbound"] or "(any non-default)"),
             "  checked IPs: %d  in-set: %d  not-in-set: %d  wrong-set: %d  unverifiable: %d" % (
                 v["checked_ips"], v["in_set"], v["not_in_set"], v["wrong_set"], v["unverifiable"]),
             "  eventual-miss: %d/%d queries (%.2f%%)  hold-timeout but eventually in-set: %d" % (
                 v["eventual_miss"], v["verified_queries"], v["eventual_miss_percent"],
                 v["eventually_in_set"]),
             "  verification took %.1fs; oldest answer was %.1fs old when checked "
             "(set entries expire after their timeout, so on long runs a miss may be an expired entry)" % (
                 v["duration_s"], v["max_age_s"])]
    if v.get("ttl_min_s") is not None:
        lines.append("  remaining entry TTL of verified IPs: min %.0fs, max %.0fs"
                     % (v["ttl_min_s"], v["ttl_max_s"]))
    if v["bad_ips"]:
        lines.append("  e.g. not in the expected set: " + ", ".join(v["bad_ips"][:5]))
    return "\n".join(lines)


# ---------------------------------------------------------------- report


def fmt(v, unit=""):
    return "-" if v is None else ("%.1f%s" % (v, unit) if isinstance(v, float) else "%d%s" % (v, unit))


def render(summary):
    lines = ["== %d queries at %s q/s ==" % (summary["total"], summary["rate"])]
    for o in OUTCOMES:
        n = summary["counts"][o]
        if n or o in ("held-ok", "hold-timeout", "bypass", "client-timeout"):
            lines.append("  %-15s %6d  %6.2f%%" % (o, n, summary["percent"][o]))
            if o == "hold-timeout" and n:
                lines.append("    hold-timeout (late write ok): %d of %d  (late_write events: %d)" % (
                    summary.get("hold_timeout_late_ok", 0), n, summary.get("late_writes", 0)))
    for key, label, unit in (("rtt_ms", "client RTT", "ms"), ("hold_us", "hold", "us"),
                             ("set_write_us", "set write", "us"), ("parse_us", "parse", "us")):
        d = summary[key]
        lines.append("  %-11s n=%-6d p50=%s p95=%s p99=%s max=%s" % (
            label, d["count"], fmt(d["p50"], unit), fmt(d["p95"], unit),
            fmt(d["p99"], unit), fmt(d["max"], unit)))
    sr, rr = summary.get("send_rate"), summary.get("recv_rate")
    if sr is not None or rr is not None:
        lines.append("  rate: requested=%s achieved send=%s recv=%s qps" % (
            summary["rate"], "-" if sr is None else "%.1f" % sr, "-" if rr is None else "%.1f" % rr))
    if summary.get("rate_warning"):
        lines.append("  WARNING: " + summary["rate_warning"])
    if summary["counter_deltas"]:
        lines.append("  daemon counters: " + ", ".join(
            "%s=%d" % kv for kv in sorted(summary["counter_deltas"].items())))
    else:
        lines.append("  daemon counters: unavailable")
    if summary["gaps"]:
        lines.append("  event stream gaps: " + ", ".join(
            "%d-%d" % (g["from_seq"], g["to_seq"]) for g in summary["gaps"]))
    if summary["stream_error"]:
        lines.append("  event stream error: " + summary["stream_error"])
    return "\n".join(lines)


def step_line(summary):
    c = summary["counts"]
    sr = summary.get("send_rate")
    return ("rate %-6s sent=%-6s total=%-6d held-ok=%.2f%% hold-timeout=%d set-error=%d "
            "bypass=%d unknown=%d not-in-set=%d wrong-set=%d timeout=%d p99rtt=%sms") % (
        summary["rate"], "-" if sr is None else "%.0f" % sr, summary["total"],
        summary["percent"]["held-ok"], c["hold-timeout"],
        c["set-error"], c["bypass"], c["unknown"], c["not-in-set"], c["wrong-set"],
        c["client-timeout"], fmt(summary["rtt_ms"]["p99"]))


# ---------------------------------------------------------------- main


def parse_args(argv):
    p = argparse.ArgumentParser(description="Measure keen-pbr DNS hold loss.")
    p.add_argument("--api", required=True, help="http://ROUTER:PORT of the keen-pbr API")
    p.add_argument("--password", help="API password (POST /api/auth/login)")
    p.add_argument("--token", help="API bearer token")
    p.add_argument("--resolver", help="DNS server to query (default: host of --api); "
                   "use e.g. 8.8.8.8 to test the transit FORWARD path")
    p.add_argument("--resolver-port", type=int, default=53)
    p.add_argument("--mode", choices=("nip", "file"), default="nip")
    p.add_argument("--domains", help="file with domain names (mode file)")
    p.add_argument("--zone", default="nip.io")
    p.add_argument("--count", type=int, default=500, help="queries per step")
    p.add_argument("--rate", type=float, default=50.0, help="queries per second")
    p.add_argument("--rates", help="comma separated rate steps, e.g. 50,200,1000")
    p.add_argument("--timeout", type=float, default=2.0)
    p.add_argument("--settle", type=float, default=3.0)
    p.add_argument("--json", help="write the full result to this file")
    p.add_argument("--skip-preflight", action="store_true")
    p.add_argument("--no-verify", action="store_true",
                   help="skip the post-run kernel set verification (POST /api/routing/test)")
    p.add_argument("--verify-rate", type=float, default=20.0,
                   help="routing/test requests per second during verification (default 20)")
    p.add_argument("--verify-ssh", metavar="TARGET",
                   help="verify via ONE ssh call (e.g. root@192.168.1.1, key login) by reading the "
                   "kernel sets directly (nft/ipset) instead of POST /api/routing/test")
    p.add_argument("--ssh-opt", action="append", default=[], metavar="OPT",
                   help="extra ssh -o option, repeatable (e.g. IdentityFile=~/.ssh/id_router)")
    p.add_argument("--ssh-bin", default="ssh", help="ssh executable (default: ssh)")
    args = p.parse_args(argv)
    if args.mode == "file" and not args.domains:
        p.error("--mode file needs --domains")
    if args.verify_rate <= 0:
        p.error("--verify-rate must be > 0")
    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    api = Api(args.api, args.token)
    try:
        if args.password and not args.token:
            api.login(args.password)
        health = api.get_json("/api/health/service")
        config = api.get_json("/api/config").get("config", {})
    except urllib.error.HTTPError as exc:
        print("API error: %s (use --password/--token if auth is enabled)" % exc, file=sys.stderr)
        return 2
    except (urllib.error.URLError, OSError, ValueError, KeyError) as exc:
        print("cannot reach API: %s" % exc, file=sys.stderr)
        return 2

    rates = [float(r) for r in args.rates.split(",")] if args.rates else [args.rate]
    if args.mode == "nip":
        probe = [args.zone]
        print("note: public %s may rate-limit high QPS; failures then show as client-timeout"
              % args.zone, file=sys.stderr)
    else:
        with open(args.domains) as f:
            probe = sorted({ln.strip().lower() for ln in f
                            if ln.strip() and not ln.lstrip().startswith("#")})[:50]
    if not args.skip_preflight:
        ok, msgs = preflight(config, health, probe)
        for m in msgs:
            print(m, file=sys.stderr)
        if not ok:
            return 2

    host = urllib.parse.urlparse(args.api).hostname
    dest = (args.resolver or host, args.resolver_port)
    try:
        dest = (socket.gethostbyname(dest[0]), dest[1])
    except OSError as exc:
        print("cannot resolve resolver address: %s" % exc, file=sys.stderr)
        return 2

    results = []
    for rate in rates:
        count = args.count
        names = make_nip_names(count, args.zone) if args.mode == "nip" \
            else load_domains(args.domains, count)
        summary, records, stream = run_step(api, dest, names, rate, args)
        print(render(summary))
        results.append((summary, records, stream))
    verification = None
    if not args.no_verify:
        exp, rule_idx, lname, cands = expected_outbound(config, probe)
        if len(cands) > 1:
            print("note: %d route rules reference the bench domains, using rule #%d (list %s, "
                  "outbound %s); others: %s" % (
                      len(cands), rule_idx, lname, exp,
                      ", ".join("#%d->%s" % (i, o) for o, i, _l in cands[1:])))
        elif cands:
            print("note: expected outbound %s (rule #%d, list %s)" % (exp, rule_idx, lname))
        else:
            print("note: could not determine the expected outbound from the config; "
                  "any non-default outbound counts as in-set")
        flat = [q for _s, records, _st in results for q in records]
        if args.verify_ssh:
            lists = []
            for _o, _i, ln in cands:
                if ln not in lists:
                    lists.append(ln)
            if not lists:
                print("--verify-ssh: cannot determine the bench list from the config", file=sys.stderr)
                return 2
            print("verifying kernel sets over ssh %s (after the run)..." % args.verify_ssh)
            try:
                verification = verify_ssh(args, flat, lists)
            except SshError as exc:
                print("ssh verification failed: %s" % exc, file=sys.stderr)
                return 2
            for name in verification["missing_sets"]:
                print("warning: kernel set %s does not exist on the router" % name, file=sys.stderr)
        else:
            print("verifying kernel sets for answer IPs (after the run, %.0f req/s)..." % args.verify_rate)
            verification = verify_sets(api, flat, exp, args.verify_rate)
        for summary, records, stream in results:
            fresh = summarize(records, stream.events)
            for key in ("counts", "percent"):
                summary[key] = fresh[key]
        print(render_verification(verification))
    if len(results) > 1 or verification:
        print("\n== summary ==")
        for summary, _r, _s in results:
            print(step_line(summary))
    if args.json:
        dump = []
        for summary, records, stream in results:
            dump.append({
                "summary": summary,
                "queries": [{k: v for k, v in q.items() if k != "event"} |
                            {"event_seq": (q["event"] or {}).get("seq")} for q in records],
                "events": [{k: v for k, v in e.items() if not k.startswith("_")}
                           for e in stream.events],
                "kernel_verification": verification,
            })
        with open(args.json, "w") as f:
            json.dump(dump, f, indent=1)
    bad = sum(s["counts"]["hold-timeout"] + s["counts"]["set-error"] + s["counts"]["bypass"]
              + s["counts"]["not-in-set"] + s["counts"]["wrong-set"]
              for s, _r, _s in results)
    if verification and (verification["not_in_set"] or verification["wrong_set"]):
        bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
