#!/usr/bin/env python3
import pathlib
import sys
import time
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent / "container"))
from case_engine import (Registry, Reporter, Runner, aggregate_status, marker,
                         truncate_diagnostic)


class EngineTest(unittest.TestCase):
    def test_registry_is_ordered_and_filters(self):
        registry = Registry()
        registry.case("both")(lambda _: None)
        registry.case("nft", ("nftables",))(lambda _: None)
        self.assertEqual(registry.names, ("both", "nft"))
        self.assertEqual([case.name for case in registry.select("all", "iptables")], ["both"])
        self.assertEqual([case.name for case in registry.select("nft,both", "nftables")],
                         ["nft", "both"])
        with self.assertRaisesRegex(ValueError, "unknown"):
            registry.select("missing", "nftables")
        with self.assertRaisesRegex(ValueError, "unsupported"):
            registry.select("nft", "iptables")

    def test_teardown_and_later_cases_run_after_failure(self):
        registry = Registry()
        calls = []
        registry.case("bad")(lambda _: (_ for _ in ()).throw(RuntimeError("boom")))
        registry.case("good")(lambda _: calls.append("good"))
        output = []
        preserved = []
        runner = Runner("nftables", registry.select("all", "nftables"), object(),
                        lambda _, case: calls.append("setup:" + case.name),
                        lambda _, case: calls.append("teardown:" + case.name),
                        lambda *_: "diagnostic", Reporter(output.append),
                        preserve=lambda case, text: preserved.append((case.name, text)))
        results = runner.run()
        self.assertEqual([result.status for result in results], ["fail", "pass"])
        self.assertIn("teardown:bad", calls)
        self.assertIn("good", calls)
        self.assertEqual(aggregate_status(results), 1)
        self.assertEqual(preserved[0][0], "bad")
        self.assertIn("diagnostic", preserved[0][1])

    def test_marker_escapes_unstructured_values(self):
        self.assertEqual(marker("EVENT", backend="nftables", message="two words\nnext"),
                         "KPBR_IT_EVENT backend=nftables message=two_words\\nnext")

    def test_diagnostic_is_bounded(self):
        value = truncate_diagnostic("\n".join(str(i) for i in range(20)), max_lines=3)
        self.assertEqual(value.splitlines()[:3], ["0", "1", "2"])
        self.assertIn("truncated", value)

    def test_aggregate_success(self):
        registry = Registry()
        registry.case("ok")(lambda _: None)
        results = Runner("iptables", registry.select("all", "iptables"), object(),
                         lambda *_: None, lambda *_: None, lambda *_: "",
                         Reporter(lambda _: None)).run()
        self.assertEqual(aggregate_status(results), 0)

    def test_timeout_tears_down_and_continues(self):
        registry = Registry()
        calls = []
        registry.case("slow")(lambda _: time.sleep(0.05))
        registry.case("next")(lambda _: calls.append("next"))
        results = Runner("iptables", registry.select("all", "iptables"), object(),
                         lambda *_: None, lambda _, case: calls.append(case.name),
                         lambda *_: "", Reporter(lambda _: None),
                         timeout_seconds=0.01).run()
        self.assertEqual([result.status for result in results], ["fail", "pass"])
        self.assertIn("TimeoutError", results[0].error)
        self.assertEqual(calls, ["slow", "next", "next"])


if __name__ == "__main__":
    unittest.main()
