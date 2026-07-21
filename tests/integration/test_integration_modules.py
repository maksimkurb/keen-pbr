#!/usr/bin/env python3
import importlib.util
import pathlib
import sys
import unittest

CONTAINER = pathlib.Path(__file__).parent / "container"
sys.path.insert(0, str(CONTAINER))

from integration_context import parse_observations, parse_probe, ssh_command
from probe import family_for
from cases.sigusr1_no_packet_leak import run_signal_cycles


def load_system_module():
    spec = importlib.util.spec_from_file_location("integration_test_system",
                                                  CONTAINER / "test-system.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class IntegrationModulesTest(unittest.TestCase):
    def test_registration_is_explicit_ordered_and_complete(self):
        module = load_system_module()
        expected = (
            "service_lifecycle", "dns_routing_save", "urltest_rebuild", "rule_shapes",
            "table_interface", "multiport_validation", "route_list", "route_proto",
            "route_dscp", "route_src_port", "route_dest_port", "route_src_addr",
            "route_dest_addr", "route_all_criteria", "dns_upstream_ipv4",
            "dns_upstream_ipv6", "sigusr1_no_packet_leak", "dns_no_leak",
        )
        registry = module.build_registry()
        self.assertEqual(registry.names, expected)
        self.assertEqual(tuple(case.name for case in registry.select("all", "nftables")), expected)
        selected = registry.select("route_dscp,dns_no_leak", "iptables")
        self.assertEqual([case.name for case in selected], ["route_dscp", "dns_no_leak"])

    def test_remote_command_quotes_each_argument(self):
        command = ssh_command("/tmp/a key", "192.0.2.2",
                              ("sh", "-c", "printf '%s' 'two words'"))
        self.assertEqual(command[-2], "root@192.0.2.2")
        self.assertEqual(command[-1], "sh -c 'printf '\"'\"'%s'\"'\"' '\"'\"'two words'\"'\"''")

    def test_probe_and_observation_parsing(self):
        output = 'noise\n{"token":"abc","identity":"wan_pbr"}\n'
        self.assertEqual(parse_probe(output, "abc")["identity"], "wan_pbr")
        observations = parse_observations(
            '{"token":"abc"}\n{"token":"def"}\n', {"def"})
        self.assertEqual(observations, [{"token": "def"}])
        with self.assertRaisesRegex(AssertionError, "mismatch"):
            parse_probe(output, "different")

    def test_dual_stack_address_detection(self):
        import socket
        self.assertEqual(family_for("198.18.0.10"), socket.AF_INET)
        self.assertEqual(family_for("2001:db8:100::10"), socket.AF_INET6)

    def test_sigusr1_cycles_are_strictly_sequenced(self):
        events = []

        def send(index):
            events.append(("send", index))
            return index + 0.5

        def wait(index, sent):
            events.append(("wait", index, sent))

        def state(index, stage):
            events.append((stage, index))

        run_signal_cycles(3, send, wait, state)
        self.assertEqual(events, [
            ("before", 0), ("send", 0), ("wait", 0, 0.5), ("after", 0),
            ("before", 1), ("send", 1), ("wait", 1, 1.5), ("after", 1),
            ("before", 2), ("send", 2), ("wait", 2, 2.5), ("after", 2),
        ])


if __name__ == "__main__":
    unittest.main()
