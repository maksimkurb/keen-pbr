"""Unit and end-to-end tests of scripts/dns-bench.py against a fake router
(HTTP API + SSE event stream + UDP DNS responder), all on localhost."""
import importlib.util
import json
import os
import queue
import random
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

_SPEC = importlib.util.spec_from_file_location(
    "dns_bench", os.path.join(os.path.dirname(__file__), "..", "scripts", "dns-bench.py"))
bench = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(bench)


def build_response(query, addr="198.18.1.2", rcode=0, with_a=True):
    qid = struct.unpack(">H", query[:2])[0]
    question = query[12:]
    flags = 0x8180 | rcode
    an = 1 if with_a else 0
    out = struct.pack(">HHHHHH", qid, flags, 1, an, 0, 0) + question
    if with_a:
        out += b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + socket.inet_aton(addr)
    return out


def answer_ip(name):
    """nip-style names resolve to the 198.18.a.b embedded in them."""
    parts = name.split(".")[0].split("-")
    if len(parts) >= 5 and parts[1:3] == ["198", "18"]:
        return "198.18.%s.%s" % (parts[3], parts[4])
    return "198.18.1.2"


class WireTests(unittest.TestCase):
    def test_encode_decode_roundtrip(self):
        q = bench.encode_query("Abc-198-18-1-2.nip.io", 0x1234)
        self.assertEqual(q[:2], b"\x12\x34")
        self.assertEqual(q[2:4], b"\x01\x00")
        resp = bench.decode_response(build_response(q))
        self.assertEqual(resp["id"], 0x1234)
        self.assertEqual(resp["qname"], "abc-198-18-1-2.nip.io")
        self.assertEqual(resp["addresses"], ["198.18.1.2"])
        self.assertEqual(resp["rcode"], 0)

    def test_decode_nodata_and_garbage(self):
        q = bench.encode_query("x.example", 1)
        self.assertEqual(bench.decode_response(build_response(q, with_a=False))["addresses"], [])
        self.assertEqual(bench.decode_response(build_response(q, rcode=3, with_a=False))["rcode"], 3)
        self.assertIsNone(bench.decode_response(b"\x00"))
        self.assertIsNone(bench.decode_response(q))  # a query, not a response

    def test_bad_label(self):
        with self.assertRaises(ValueError):
            bench.encode_query("a" * 64 + ".com", 1)


class HelperTests(unittest.TestCase):
    def test_nip_names(self):
        names = bench.make_nip_names(200, "nip.io", random.Random(1))
        self.assertEqual(len(set(names)), 200)
        for n in names:
            self.assertRegex(n, r"^[a-z0-9]{8}-198-18-\d{1,3}-\d{1,3}\.nip\.io$")

    def test_percentile(self):
        data = list(range(1, 101))
        self.assertEqual(bench.percentile(data, 50), 50)
        self.assertEqual(bench.percentile(data, 95), 95)
        self.assertEqual(bench.percentile(data, 99), 99)
        self.assertEqual(bench.percentile(data, 100), 100)
        self.assertEqual(bench.percentile([7], 99), 7)
        self.assertIsNone(bench.percentile([], 50))

    def test_domain_match_and_preflight(self):
        cfg = {"lists": {"b": {"domains": ["*.nip.io"]}, "other": {"domains": ["x.com"]}},
               "route": {"rules": [{"list": ["b"], "outbound": "o"}]}}
        health = {"intercept": {"dns_hold_active": True}}
        ok, _ = bench.preflight(cfg, health, ["nip.io"])
        self.assertTrue(ok)
        cfg["route"]["rules"][0]["enabled"] = False
        ok, msgs = bench.preflight(cfg, health, ["nip.io"])
        self.assertFalse(ok)
        self.assertIn("dns_bench", msgs[0])
        cfg["route"]["rules"][0]["enabled"] = True
        ok, msgs = bench.preflight(cfg, {"intercept": {"dns_hold_active": False}}, ["nip.io"])
        self.assertFalse(ok)
        ok, _ = bench.preflight(cfg, health, ["nip.io", "other.org"])
        self.assertFalse(ok)

    def test_counter_deltas(self):
        before = {"counters": {"dns_packets": 10, "dns_matched": 9, "dns_hold_timeouts": 1,
                               "set_errors": 0, "queue_overruns": 0},
                  "kernel_queue": {"queue_dropped": 0, "user_dropped": 1, "id_sequence": 100}}
        after = {"counters": {"dns_packets": 30, "dns_matched": 29, "dns_hold_timeouts": 3,
                              "set_errors": 1, "queue_overruns": 0},
                 "kernel_queue": {"queue_dropped": 2, "user_dropped": 1, "id_sequence": 125}}
        d = bench.counter_deltas(before, after)
        self.assertEqual(d["dns_packets"], 20)
        self.assertEqual(d["dns_hold_timeouts"], 2)
        self.assertEqual(d["kernel_id_sequence"], 25)
        self.assertEqual(d["kernel_unprocessed"], 5)
        self.assertEqual(bench.counter_deltas(None, after), {})


def Q(name, t, answered=True, addrs=("198.18.1.1",), late=False):
    return {"name": name, "t_send": t, "t_recv": t + 0.01 if answered else None,
            "rtt": 0.01 if answered else None, "addresses": list(addrs) if answered else [],
            "late": late}


def EV(seq, domain, **kw):
    e = {"type": "INTERCEPT", "seq": seq, "source": "dns", "domain": domain, "added": 1,
         "refreshed": 0, "errors": 0, "hold_us": 100, "timed_out": False, "_arrival": 100.0}
    e.update(kw)
    return e


class ClassifyTests(unittest.TestCase):
    def outcomes(self, queries, events, gaps=(), closed=None):
        bench.classify(queries, events, list(gaps), closed)
        return [q["outcome"] for q in queries]

    def test_each_outcome(self):
        queries = [Q("a.x", 1), Q("b.x", 2), Q("c.x", 3), Q("d.x", 4), Q("e.x", 5),
                   Q("f.x", 6, answered=False), Q("g.x", 7, addrs=()), Q("h.x", 8)]
        events = [EV(1, "a.x"), EV(2, "b.x", timed_out=True), EV(3, "c.x", errors=2, added=0),
                  EV(4, "d.x", added=0, refreshed=0), EV(9, "H.X", refreshed=1, added=0)]
        self.assertEqual(self.outcomes(queries, events),
                         ["held-ok", "hold-timeout", "set-error", "no-write", "bypass",
                          "client-timeout", "nodata", "held-ok"])

    def test_gap_makes_unknown(self):
        queries = [Q("a.x", 10.0), Q("b.x", 50.0)]
        gap = {"from_seq": 5, "to_seq": 9, "_arrival": 10.5, "_prev_arrival": 9.8}
        self.assertEqual(self.outcomes(queries, [], [gap]), ["unknown", "bypass"])

    def test_closed_stream_makes_unknown(self):
        queries = [Q("a.x", 1.0), Q("b.x", 20.0)]
        self.assertEqual(self.outcomes(queries, [], closed=10.0), ["bypass", "unknown"])

    def test_repeated_name_consumes_events_in_order(self):
        queries = [Q("a.x", 1), Q("a.x", 2)]
        events = [EV(1, "a.x", timed_out=True), EV(2, "a.x")]
        self.assertEqual(self.outcomes(queries, events), ["hold-timeout", "held-ok"])

    def test_late_answer_is_timeout(self):
        self.assertEqual(self.outcomes([Q("a.x", 1, late=True)], [EV(1, "a.x")]),
                         ["client-timeout"])

    def test_non_dns_events_ignored(self):
        self.assertEqual(self.outcomes([Q("a.x", 1)], [EV(1, "a.x", source="sni")]), ["bypass"])

    def test_summarize(self):
        queries = [Q("a.x", 1), Q("b.x", 2)]
        events = [EV(1, "a.x", set_write_us=40, parse_us=5), EV(2, "b.x", timed_out=True)]
        bench.classify(queries, events, [])
        s = bench.summarize(queries, events)
        self.assertEqual(s["counts"]["held-ok"], 1)
        self.assertEqual(s["counts"]["hold-timeout"], 1)
        self.assertEqual(s["percent"]["held-ok"], 50.0)
        self.assertEqual(s["hold_us"]["count"], 2)
        self.assertEqual(s["set_write_us"]["max"], 40)


# ----------------------------------------------------------------- fake router


class FakeRouter:
    """HTTP API + SSE + UDP DNS on localhost.  `behavior(name)` returns one of
    held, timeout, error, bypass, drop, gap for each queried name."""

    def __init__(self, behavior, password="secret", dns_hold_active=True, config=None,
                 membership=None):
        self.behavior = behavior
        # membership(ip) -> actual_outbound reported by /api/routing/test; a value
        # of None makes the endpoint fail with HTTP 500.
        self.membership = membership or (lambda ip: "o")
        self.last_dns = 0.0
        self.log = []  # ordered ("dns"|"verify", detail)
        self.password = password
        self.dns_hold_active = dns_hold_active
        self.config = config if config is not None else {
            "lists": {"dns_bench": {"domains": ["nip.io"]}},
            "route": {"rules": [{"list": ["dns_bench"], "outbound": "o"}]}}
        self.lock = threading.Lock()
        self.seq = 0
        self.dns_packets = 0
        self.subs = []
        self.tokens = {"tok123"}
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.udp.settimeout(0.2)
        self.stop_flag = threading.Event()
        router = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *a):
                pass

            def _json(self, obj, code=200):
                body = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def _authed(self):
                return self.headers.get("Authorization", "") == "Bearer tok123"

            def do_POST(self):
                length = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(length) or b"{}")
                if self.path == "/api/auth/login" and body.get("password") == router.password:
                    return self._json({"token": "tok123", "expires_at": 1})
                if self.path == "/api/routing/test" and self._authed():
                    ip = body.get("target")
                    with router.lock:
                        router.log.append(("verify", ip))
                    actual = router.membership(ip)
                    if actual is None:
                        return self._json({"error": "boom"}, 500)
                    return self._json({"target": ip, "is_domain": False, "results": [
                        {"ip": ip, "expected_outbound": "o", "actual_outbound": actual,
                         "ok": actual == "o"}]})
                self._json({"error": "bad"}, 401)

            def do_GET(self):
                if not self._authed():
                    return self._json({"error": "unauthorized"}, 401)
                if self.path == "/api/health/service":
                    with router.lock:
                        return self._json({"intercept": {
                            "dns_hold_active": router.dns_hold_active, "reasons": [],
                            "counters": {"dns_packets": router.dns_packets,
                                         "dns_matched": router.dns_packets,
                                         "dns_hold_timeouts": 0, "set_errors": 0,
                                         "queue_overruns": 0},
                            "kernel_queue": {"queue_total": 0, "queue_dropped": 0,
                                             "user_dropped": 0,
                                             "id_sequence": router.dns_packets}}})
                if self.path == "/api/config":
                    return self._json({"config": router.config, "is_draft": False})
                if urlsplit(self.path).path == "/api/dns/test":
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    self.end_headers()
                    q = queue.Queue()
                    with router.lock:
                        router.subs.append(q)
                    try:
                        self.wfile.write(b'data: {"type":"HELLO"}\n\n')
                        self.wfile.flush()
                        while not router.stop_flag.is_set():
                            try:
                                msg = q.get(timeout=0.1)
                            except queue.Empty:
                                continue
                            if msg is None:
                                break
                            self.wfile.write(b"data: " + msg.encode() + b"\n\n")
                            self.wfile.flush()
                    except OSError:
                        pass
                    finally:
                        with router.lock:
                            if q in router.subs:
                                router.subs.remove(q)
                    return
                self._json({}, 404)

        self.http = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.http.daemon_threads = True
        self.threads = [threading.Thread(target=self.http.serve_forever, daemon=True),
                        threading.Thread(target=self._dns_loop, daemon=True)]
        for t in self.threads:
            t.start()

    @property
    def api_url(self):
        return "http://127.0.0.1:%d" % self.http.server_address[1]

    @property
    def dns_port(self):
        return self.udp.getsockname()[1]

    def _publish(self, obj):
        text = json.dumps(obj)
        with self.lock:
            for q in self.subs:
                q.put(text)

    def _dns_loop(self):
        while not self.stop_flag.is_set():
            try:
                data, addr = self.udp.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            msg = bench.decode_response(b"\x00" * 0 + data[:2] + b"\x80\x00" + data[4:])
            name = msg["qname"] if msg else ""
            action = self.behavior(name)
            with self.lock:
                self.log.append(("dns", name))
                self.last_dns = time.time()
                self.dns_packets += 1
                if action not in ("drop", "bypass"):
                    self.seq += 1  # only queries that produce an event consume a seq
                seq = self.seq
            if action == "drop":
                continue
            ev = {"type": "INTERCEPT", "seq": seq, "ts_ms": 1, "source": "dns", "domain": name,
                  "lists": ["dns_bench"], "ips": ["198.18.1.2"], "added": 1, "refreshed": 0,
                  "errors": 0, "hold_us": 150, "parse_us": 8, "set_write_us": 90,
                  "timed_out": False}
            if action == "timeout":
                ev.update(timed_out=True, errors=1, added=0)
            elif action == "error":
                ev.update(errors=1, added=0)
            if action == "gap":
                self._publish({"type": "GAP", "from_seq": seq, "to_seq": seq})
            elif action != "bypass":
                self._publish(ev)
            self.udp.sendto(build_response(data, addr=answer_ip(name)), addr)

    def close(self):
        self.stop_flag.set()
        with self.lock:
            for q in self.subs:
                q.put(None)
        self.http.shutdown()
        self.http.server_close()
        self.udp.close()


class Args:
    timeout = 0.5
    settle = 0.3


class EndToEndTests(unittest.TestCase):
    def run_main(self, router, extra=()):
        argv = ["--api", router.api_url, "--password", "secret", "--resolver", "127.0.0.1",
                "--resolver-port", str(router.dns_port), "--settle", "0.4", "--timeout", "0.5",
                *extra]
        return bench.main(argv)

    def test_classification_end_to_end(self):
        def behavior(name):
            label = name.split("-")[0]
            c = label[0]
            return {"a": "timeout", "b": "error", "c": "bypass", "d": "drop"}.get(c, "held")
        router = FakeRouter(behavior)
        self.addCleanup(router.close)
        api = bench.Api(router.api_url)
        api.login("secret")
        names = bench.make_nip_names(120, "nip.io", random.Random(7))
        args = Args()
        summary, records, _stream = bench.run_step(
            api, ("127.0.0.1", router.dns_port), names, 400, args)
        expected = {}
        for n in names:
            o = {"a": "hold-timeout", "b": "set-error", "c": "bypass",
                 "d": "client-timeout"}.get(n[0], "held-ok")
            expected[o] = expected.get(o, 0) + 1
        for o in bench.OUTCOMES:
            self.assertEqual(summary["counts"][o], expected.get(o, 0), o)
        self.assertEqual(summary["total"], 120)
        self.assertEqual(summary["counter_deltas"]["dns_packets"], 120)
        self.assertGreater(summary["hold_us"]["count"], 0)
        self.assertEqual(summary["set_write_us"]["p50"], 90)

    def test_gap_events_make_unknown(self):
        router = FakeRouter(lambda name: "gap" if name[0] in "abcdefgh" else "held")
        self.addCleanup(router.close)
        api = bench.Api(router.api_url)
        api.login("secret")
        names = bench.make_nip_names(80, "nip.io", random.Random(3))
        summary, _r, _s = bench.run_step(api, ("127.0.0.1", router.dns_port), names, 200, Args())
        gapped = sum(1 for n in names if n[0] in "abcdefgh")
        self.assertGreater(gapped, 0)
        self.assertEqual(summary["counts"]["unknown"], gapped)
        self.assertEqual(summary["counts"]["bypass"], 0)
        self.assertEqual(summary["counts"]["held-ok"], 80 - gapped)

    def test_main_steps_and_json(self):
        router = FakeRouter(lambda name: "held")
        self.addCleanup(router.close)
        out = os.path.join(os.environ.get("TMPDIR", "/tmp"), "dns-bench-test-%d.json" % os.getpid())
        self.addCleanup(lambda: os.path.exists(out) and os.remove(out))
        rc = self.run_main(router, ["--rates", "100,300", "--count", "40", "--json", out])
        self.assertEqual(rc, 0)
        with open(out) as f:
            data = json.load(f)
        self.assertEqual(len(data), 2)
        self.assertEqual(data[0]["summary"]["counts"]["held-ok"], 40)
        self.assertEqual(data[1]["summary"]["rate"], 300.0)
        self.assertEqual(len(data[0]["queries"]), 40)

    def test_file_mode(self):
        router = FakeRouter(lambda name: "held",
                            config={"lists": {"l": {"domains": ["example.com"]}},
                                    "route": {"rules": [{"list": ["l"], "outbound": "o"}]}})
        self.addCleanup(router.close)
        path = os.path.join(os.environ.get("TMPDIR", "/tmp"), "dns-bench-dom-%d.txt" % os.getpid())
        with open(path, "w") as f:
            f.write("# c\nexample.com\nwww.example.com\n")
        self.addCleanup(os.remove, path)
        rc = self.run_main(router, ["--mode", "file", "--domains", path, "--count", "20", "--rate", "100"])
        self.assertEqual(rc, 0)

    def test_preflight_failures_exit_2(self):
        router = FakeRouter(lambda name: "held", config={"lists": {}, "route": {"rules": []}})
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--count", "5"]), 2)
        router2 = FakeRouter(lambda name: "held", dns_hold_active=False)
        self.addCleanup(router2.close)
        self.assertEqual(self.run_main(router2, ["--count", "5"]), 2)

    def test_bad_password_exit_2(self):
        router = FakeRouter(lambda name: "held")
        self.addCleanup(router.close)
        argv = ["--api", router.api_url, "--password", "wrong"]
        self.assertEqual(bench.main(argv), 2)

    def test_problems_give_nonzero_exit(self):
        router = FakeRouter(lambda name: "timeout")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--count", "10", "--rate", "100"]), 1)


class VerifyUnitTests(unittest.TestCase):
    def test_verdict_for(self):
        v = bench.verdict_for
        self.assertEqual(v("o", "o"), "in-set")
        self.assertEqual(v("other", "o"), "wrong-set")
        self.assertEqual(v("(default)", "o"), "not-in-set")
        self.assertEqual(v("(unknown)", "o"), "unverifiable")
        self.assertEqual(v(None, "o"), "unverifiable")
        self.assertEqual(v("anything", None), "in-set")
        self.assertEqual(v("(default)", None), "not-in-set")

    def test_expected_outbound_first_matching_rule(self):
        cfg = {"lists": {"a": {"domains": ["x.com"]}, "b": {"domains": ["nip.io"]},
                         "c": {"domains": ["nip.io"]}},
               "route": {"rules": [{"list": ["a"], "outbound": "wa"},
                                   {"list": ["b"], "outbound": "wb", "enabled": False},
                                   {"list": ["c"], "outbound": "wc"},
                                   {"list": ["b", "c"], "outbound": "wd"}]}}
        out, idx, lname, cands = bench.expected_outbound(cfg, ["nip.io"])
        self.assertEqual((out, idx, lname), ("wc", 2, "c"))
        self.assertEqual(len(cands), 2)
        self.assertEqual(bench.expected_outbound(cfg, ["zzz.org"])[0], None)

    def test_verify_sets_overrides_and_pacing(self):
        class FakeApi:
            def __init__(self):
                self.calls = []

            def post_json(self, path, data, timeout=None):
                self.calls.append(data["target"])
                actual = {"1.1.1.1": "o", "2.2.2.2": "(default)", "3.3.3.3": "x",
                          "4.4.4.4": "(unknown)"}[data["target"]]
                return {"results": [{"ip": data["target"], "actual_outbound": actual}]}

        def R(name, ip, outcome, t):
            return {"name": name, "addresses": [ip], "outcome": outcome, "t_recv": t}
        recs = [R("a", "1.1.1.1", "held-ok", 1.0), R("b", "2.2.2.2", "held-ok", 2.0),
                R("c", "3.3.3.3", "held-ok", 3.0), R("d", "4.4.4.4", "held-ok", 4.0),
                R("e", "1.1.1.1", "hold-timeout", 5.0), R("f", "2.2.2.2", "hold-timeout", 6.0),
                R("g", "9.9.9.9", "nodata", 7.0)]
        sleeps = []
        now = [100.0]
        api = FakeApi()
        v = bench.verify_sets(api, recs, "o", 10, sleep=lambda d: (sleeps.append(d),
                              now.__setitem__(0, now[0] + d)), clock=lambda: now[0])
        self.assertEqual(sorted(api.calls), ["1.1.1.1", "2.2.2.2", "3.3.3.3", "4.4.4.4"])
        self.assertEqual(len(sleeps), 3)  # paced at 10 req/s
        self.assertEqual([r["outcome"] for r in recs],
                         ["held-ok", "not-in-set", "wrong-set", "held-ok",
                          "hold-timeout", "hold-timeout", "nodata"])
        self.assertEqual((v["checked_ips"], v["in_set"], v["not_in_set"], v["wrong_set"],
                          v["unverifiable"]), (4, 1, 1, 1, 1))
        self.assertEqual(v["eventually_in_set"], 1)   # e
        self.assertEqual(v["eventual_miss"], 2)       # b, f
        self.assertGreater(v["max_age_s"], 90)


class RateTests(unittest.TestCase):
    def test_achieved_rates_fake_clock(self):
        recs = [{"t_send": i * 0.1, "t_recv": i * 0.1 + 0.5} for i in range(11)]
        r = bench.achieved_rates(recs)
        self.assertAlmostEqual(r["send_rate"], 10.0)
        self.assertAlmostEqual(r["recv_rate"], 10.0)
        self.assertIsNone(bench.achieved_rates([{"t_send": 1.0, "t_recv": None}])["send_rate"])
        recs[3]["t_recv"] = None
        self.assertAlmostEqual(bench.achieved_rates(recs)["recv_rate"], 9 / 1.0)

    def test_rate_warning(self):
        self.assertIsNone(bench.rate_warning(100, 95))
        self.assertIsNone(bench.rate_warning(100, None))
        msg = bench.rate_warning(100, 50)
        self.assertIn("achieved 50 qps", msg)
        self.assertIn("the client, not keen-pbr, is the bottleneck", msg)

    def test_run_step_reports_rates(self):
        router = FakeRouter(lambda name: "held")
        self.addCleanup(router.close)
        api = bench.Api(router.api_url)
        api.login("secret")
        names = bench.make_nip_names(30, "nip.io", random.Random(5))
        summary, _r, _s = bench.run_step(api, ("127.0.0.1", router.dns_port), names, 100, Args())
        self.assertAlmostEqual(summary["send_rate"], 100, delta=20)
        self.assertIsNone(summary["rate_warning"])
        # an absurd requested rate cannot be reached by the python client -> warning
        names = bench.make_nip_names(300, "nip.io", random.Random(6))
        summary, _r, _s = bench.run_step(api, ("127.0.0.1", router.dns_port), names, 1e7, Args())
        self.assertIn("bottleneck", summary["rate_warning"])


class VerifyEndToEndTests(unittest.TestCase):
    def run_main(self, router, extra=()):
        argv = ["--api", router.api_url, "--password", "secret", "--resolver", "127.0.0.1",
                "--resolver-port", str(router.dns_port), "--settle", "0.3", "--timeout", "0.5",
                "--count", "30", "--rate", "200", "--verify-rate", "500", *extra]
        return bench.main(argv)

    def test_all_in_set(self):
        router = FakeRouter(lambda name: "held")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router), 0)
        self.assertGreater(len([e for e in router.log if e[0] == "verify"]), 0)

    def test_verification_strictly_after_last_query(self):
        router = FakeRouter(lambda name: "held")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--rates", "100,300"]), 0)
        kinds = [k for k, _d in router.log]
        self.assertIn("verify", kinds)
        last_dns = max(i for i, k in enumerate(kinds) if k == "dns")
        first_verify = min(i for i, k in enumerate(kinds) if k == "verify")
        self.assertLess(last_dns, first_verify)
        # the target is the answer IP, never a domain
        for kind, detail in router.log:
            if kind == "verify":
                self.assertRegex(detail, r"^198\.18\.\d+\.\d+$")

    def test_missing_ip_gives_not_in_set_and_exit_1(self):
        out = os.path.join(os.environ.get("TMPDIR", "/tmp"), "dns-bench-v-%d.json" % os.getpid())
        self.addCleanup(lambda: os.path.exists(out) and os.remove(out))
        router = FakeRouter(lambda name: "held",
                            membership=lambda ip: "(default)" if int(ip.split(".")[2]) % 2 else "o")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--json", out]), 1)
        with open(out) as f:
            data = json.load(f)
        ver = data[0]["kernel_verification"]
        self.assertGreater(ver["not_in_set"], 0)
        self.assertGreater(data[0]["summary"]["counts"]["not-in-set"], 0)
        self.assertEqual(data[0]["summary"]["counts"]["held-ok"] +
                         data[0]["summary"]["counts"]["not-in-set"], 30)
        self.assertGreater(ver["eventual_miss_percent"], 0)

    def test_wrong_outbound_exit_1(self):
        router = FakeRouter(lambda name: "held", membership=lambda ip: "other")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router), 1)

    def test_unknown_is_unverifiable_not_failure(self):
        out = os.path.join(os.environ.get("TMPDIR", "/tmp"), "dns-bench-u-%d.json" % os.getpid())
        self.addCleanup(lambda: os.path.exists(out) and os.remove(out))
        router = FakeRouter(lambda name: "held", membership=lambda ip: "(unknown)")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--json", out]), 0)
        with open(out) as f:
            ver = json.load(f)[0]["kernel_verification"]
        self.assertEqual(ver["unverifiable"], ver["checked_ips"])
        # HTTP errors are unverifiable too
        router2 = FakeRouter(lambda name: "held", membership=lambda ip: None)
        self.addCleanup(router2.close)
        self.assertEqual(self.run_main(router2), 0)

    def test_no_verify_skips(self):
        router = FakeRouter(lambda name: "held", membership=lambda ip: "(default)")
        self.addCleanup(router.close)
        self.assertEqual(self.run_main(router, ["--no-verify"]), 0)
        self.assertEqual([e for e in router.log if e[0] == "verify"], [])


NFT_ALL = {"nftables": [{"metainfo": {}}, {"set": {"family": "inet", "name": "s", "elem": [
    {"prefix": {"addr": "198.18.0.0", "len": 16}}]}}]}
NFT_HALF = {"nftables": [{"set": {"name": "s", "elem": [
    {"elem": {"val": {"range": ["198.18.0.0", "198.18.127.255"]}, "timeout": 300, "expires": 120}},
    "198.18.200.5"]}}]}


class FakeSsh:
    """A python script standing in for ssh: records argv, stdin and a timestamp
    in a log file and prints canned stdout."""

    def __init__(self, test, stdout, rc=0, stderr=""):
        self.dir = tempfile.mkdtemp(prefix="fake-ssh-")
        test.addCleanup(lambda: __import__("shutil").rmtree(self.dir, ignore_errors=True))
        self.log = os.path.join(self.dir, "log.jsonl")
        self.path = os.path.join(self.dir, "ssh")
        with open(self.path, "w") as f:
            f.write("#!%s\nimport json, sys, time\n"
                    "with open(%r, 'a') as l:\n"
                    "    l.write(json.dumps({'argv': sys.argv[1:], 'stdin': sys.stdin.read(), "
                    "'t': time.time()}) + '\\n')\n"
                    "sys.stderr.write(%r)\nsys.stdout.write(%r)\nsys.exit(%d)\n"
                    % (sys.executable, self.log, stderr, stdout, rc))
        os.chmod(self.path, 0o755)

    def calls(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as f:
            return [json.loads(ln) for ln in f]


def nft_reply(doc, backend="nft", sets=("kpbr4d_dns_bench",), missing=("kpbr6d_dns_bench",)):
    out = "@@BACKEND %s\n" % backend
    for n in sets:
        out += "@@SET %s\n%s\n" % (n, doc if isinstance(doc, str) else json.dumps(doc))
    for n in missing:
        out += "@@SET %s\n@@MISSING %s\n" % (n, n)
    return out + "@@END\n"


class SshParseTests(unittest.TestCase):
    def test_parse_nft_forms(self):
        doc = {"nftables": [{"set": {"elem": [
            "1.2.3.4", {"prefix": {"addr": "10.0.0.0", "len": 8}},
            {"range": ["192.168.0.1", "192.168.0.9"]},
            {"elem": {"val": "5.6.7.8", "timeout": 60, "expires": 33}},
            {"elem": {"val": {"prefix": {"addr": "2001:db8::", "len": 32}}, "expires": 5}}]}}]}
        ix = bench.SetIndex(bench.parse_nft_set(json.dumps(doc)))
        self.assertEqual(ix.lookup("1.2.3.4"), (True, None))
        self.assertEqual(ix.lookup("10.9.9.9"), (True, None))
        self.assertTrue(ix.lookup("192.168.0.5")[0])
        self.assertFalse(ix.lookup("192.168.0.10")[0])
        self.assertEqual(ix.lookup("5.6.7.8"), (True, 33))
        self.assertEqual(ix.lookup("2001:db8::1"), (True, 5))
        self.assertFalse(ix.lookup("1.2.3.5")[0])

    def test_parse_ipset_save(self):
        text = ("create kpbr4d_x hash:ip family inet timeout 300\n"
                "add kpbr4d_x 198.18.1.2 timeout 250\nadd kpbr4d_x 198.19.0.0/16\n")
        ix = bench.SetIndex(bench.parse_ipset_save(text))
        self.assertEqual(ix.lookup("198.18.1.2"), (True, 250))
        self.assertTrue(ix.lookup("198.19.5.5")[0])
        self.assertFalse(ix.lookup("198.18.1.3")[0])

    def test_remote_script_is_posix_and_lists_both_families(self):
        names = bench.dynamic_set_names(["dns_bench"])
        self.assertEqual(names, ["kpbr4d_dns_bench", "kpbr6d_dns_bench"])
        script = bench.SSH_SCRIPT.format(sets=" ".join(names))
        for bashism in ("[[", "jq", "<<<", "function "):
            self.assertNotIn(bashism, script)
        self.assertIn("nft -j list set inet KeenPbrTable", script)
        self.assertIn("ipset save", script)


class VerifySshEndToEndTests(unittest.TestCase):
    def run_main(self, router, ssh, extra=()):
        argv = ["--api", router.api_url, "--password", "secret", "--resolver", "127.0.0.1",
                "--resolver-port", str(router.dns_port), "--settle", "0.3", "--timeout", "0.5",
                "--count", "30", "--rate", "200", "--verify-ssh", "root@192.0.2.1",
                "--ssh-bin", ssh.path, *extra]
        return bench.main(argv)

    def router(self, **kw):
        r = FakeRouter(lambda name: "held", **kw)
        self.addCleanup(r.close)
        return r

    def json_out(self, name):
        out = os.path.join(tempfile.gettempdir(), "dns-bench-ssh-%s-%d.json" % (name, os.getpid()))
        self.addCleanup(lambda: os.path.exists(out) and os.remove(out))
        return out

    def test_nft_all_in_set_single_call_after_queries(self):
        router = self.router(membership=lambda ip: "(default)")  # API must not be used
        ssh = FakeSsh(self, nft_reply(NFT_ALL))
        out = self.json_out("nft")
        self.assertEqual(self.run_main(router, ssh, ["--json", out, "--ssh-opt", "IdentityFile=/k"]), 0)
        calls = ssh.calls()
        self.assertEqual(len(calls), 1)
        argv = calls[0]["argv"]
        self.assertIn("BatchMode=yes", argv)
        self.assertIn("IdentityFile=/k", argv)
        self.assertEqual(argv[-3:], ["root@192.0.2.1", "sh", "-s"])
        self.assertIn("kpbr4d_dns_bench", calls[0]["stdin"])
        self.assertIn("kpbr6d_dns_bench", calls[0]["stdin"])
        self.assertGreater(calls[0]["t"], router.last_dns)
        self.assertEqual([e for e in router.log if e[0] == "verify"], [])
        with open(out) as f:
            ver = json.load(f)[0]["kernel_verification"]
        self.assertEqual(ver["method"], "ssh nft")
        self.assertEqual(ver["not_in_set"], 0)
        self.assertEqual(ver["in_set"], ver["checked_ips"])
        self.assertEqual(ver["missing_sets"], ["kpbr6d_dns_bench"])

    def test_nft_partial_not_in_set_exit_1_and_ttl(self):
        router = self.router()
        ssh = FakeSsh(self, nft_reply(NFT_HALF))
        out = self.json_out("half")
        self.assertEqual(self.run_main(router, ssh, ["--json", out]), 1)
        with open(out) as f:
            data = json.load(f)[0]
        ver = data["kernel_verification"]
        self.assertGreater(ver["not_in_set"], 0)
        self.assertGreater(ver["in_set"], 0)
        self.assertEqual(ver["ttl_min_s"], 120)
        self.assertGreater(data["summary"]["counts"]["not-in-set"], 0)

    def test_ipset_path(self):
        text = "create kpbr4d_dns_bench hash:ip timeout 300\nadd kpbr4d_dns_bench 198.18.0.0/16 timeout 77\n"
        router = self.router()
        ssh = FakeSsh(self, nft_reply(text, backend="ipset"))
        out = self.json_out("ipset")
        self.assertEqual(self.run_main(router, ssh, ["--json", out]), 0)
        with open(out) as f:
            ver = json.load(f)[0]["kernel_verification"]
        self.assertEqual(ver["method"], "ssh ipset")
        self.assertEqual(ver["ttl_min_s"], 77)

    def test_missing_set_is_unverifiable(self):
        router = self.router()
        ssh = FakeSsh(self, nft_reply(NFT_ALL, sets=(), missing=("kpbr4d_dns_bench", "kpbr6d_dns_bench")))
        out = self.json_out("missing")
        self.assertEqual(self.run_main(router, ssh, ["--json", out]), 0)
        with open(out) as f:
            ver = json.load(f)[0]["kernel_verification"]
        self.assertEqual(ver["unverifiable"], ver["checked_ips"])
        self.assertEqual(len(ver["missing_sets"]), 2)

    def test_ssh_failure_exit_2(self):
        router = self.router()
        ssh = FakeSsh(self, "", rc=255, stderr="Permission denied (publickey)")
        self.assertEqual(self.run_main(router, ssh), 2)
        self.assertEqual(len(ssh.calls()), 1)

    def test_no_backend_exit_2(self):
        router = self.router()
        ssh = FakeSsh(self, "@@BACKEND none\n@@END\n")
        self.assertEqual(self.run_main(router, ssh), 2)

    def test_no_verify_skips_ssh(self):
        router = self.router()
        ssh = FakeSsh(self, nft_reply(NFT_ALL))
        self.assertEqual(self.run_main(router, ssh, ["--no-verify"]), 0)
        self.assertEqual(ssh.calls(), [])


if __name__ == "__main__":
    unittest.main()
