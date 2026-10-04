"""Unit and end-to-end tests of scripts/dns-bench.py against a fake router
(HTTP API + SSE event stream + UDP DNS responder), all on localhost."""
import importlib.util
import json
import os
import queue
import random
import socket
import struct
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

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

    def __init__(self, behavior, password="secret", dns_hold_active=True, config=None):
        self.behavior = behavior
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
                if self.path == "/api/dns/test":
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
            self.udp.sendto(build_response(data), addr)

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


if __name__ == "__main__":
    unittest.main()
