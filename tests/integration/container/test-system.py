#!/usr/bin/env python3
"""Packaged-system integration cases for the iptables and nftables backends."""

from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

from case_engine import Registry, Runner, aggregate_status, write_summary

API = "http://127.0.0.1:12121"
LAN = "kpbr-lan"
TEST_IP = "198.18.0.10"
CONTAINER_DIR = pathlib.Path(__file__).resolve().parent
BASE_CONFIG = CONTAINER_DIR / "config.json"
TOPOLOGY = CONTAINER_DIR / "topology.sh"
CONFIG_PATH = pathlib.Path("/etc/keen-pbr/config.json")


class SystemContext:
    def __init__(self, backend: str) -> None:
        self.backend = backend

    def run(self, *args: str, check: bool = True, timeout: int = 30):
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=timeout)
        if check and result.returncode:
            raise AssertionError(
                f"command failed ({' '.join(args)}, exit {result.returncode}): "
                f"{result.stderr.strip()}")
        return result

    def api(self, path: str, method: str = "GET", payload=None):
        data = None if payload is None else json.dumps(payload).encode()
        request = urllib.request.Request(API + path, data=data, method=method,
                                         headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=8) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            body = error.read().decode(errors="replace")
            raise AssertionError(f"{method} {path} returned HTTP {error.code}: {body}") from error

    def wait_for(self, description, predicate, timeout: float = 35):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                last = predicate()
                if last:
                    return last
            except (AssertionError, OSError, urllib.error.URLError,
                    json.JSONDecodeError, subprocess.TimeoutExpired) as exc:
                last = exc
            time.sleep(0.25)
        raise AssertionError(f"timed out waiting for {description}: {last}")

    def health_running(self):
        health = self.api("/api/health/service")
        assert health["status"] == "running", health
        assert health["runtime_state"] == "running", health
        return health

    def selected_outbound(self, tag="auto", selected="wan_pbr"):
        state = self.api("/api/runtime/outbounds")
        outbound = next((item for item in state["outbounds"] if item["tag"] == tag), None)
        assert outbound is not None, state
        interface = next((item for item in outbound["interfaces"]
                          if item["outbound_tag"] == selected), None)
        assert interface is not None and interface["status"] == "active", state
        return state

    def resolve(self, domain: str, expected_ip: str = TEST_IP):
        output = self.run("ip", "netns", "exec", LAN, "dig", "+short", domain,
                          "@192.0.2.1").stdout
        assert expected_ip in output.split(), output

    def http_identity(self, domain: str, destination_ip: str = TEST_IP):
        output = self.run(
            "ip", "netns", "exec", LAN, "curl", "--fail", "--silent", "--show-error",
            "--noproxy", "*", "--resolve", f"{domain}:18080:{destination_ip}",
            "--max-time", "8", f"http://{domain}:18080/test").stdout
        payload = json.loads(output)
        assert payload["peer"] == "192.0.2.2", payload
        return payload["outbound"]

    def assert_http_outbound(self, domain: str, expected: str,
                             destination_ip: str = TEST_IP):
        def matches():
            actual = self.http_identity(domain, destination_ip)
            assert actual == expected, f"{domain} used {actual!r}, expected {expected!r}"
            return True
        self.wait_for(f"{domain} route via {expected}", matches, timeout=12)

    def dynamic_set_contains(self):
        if self.backend == "iptables":
            self.run("ipset", "test", "kpbr4d_routed", TEST_IP)
        else:
            output = self.run("nft", "list", "set", "inet", "KeenPbrTable",
                              "kpbr4d_routed").stdout
            assert TEST_IP in output, output
        return True

    def apply_config(self, config: dict):
        staged = self.api("/api/config", "POST", config)
        assert staged["status"] == "ok", staged
        saved = self.api("/api/config/save", "POST")
        assert saved["status"] == "ok" and saved["saved"] and saved["applied"], saved
        health = self.wait_for("service after config apply", self.health_running)
        routing = self.api("/api/health/routing")
        assert routing["overall"] == "ok", routing
        assert routing["firewall_backend"] == self.backend, routing
        return health

    def firewall_text(self) -> str:
        if self.backend == "iptables":
            return self.run("iptables-save", "-t", "mangle").stdout + self.run(
                "ip6tables-save", "-t", "mangle", check=False).stdout
        return self.run("nft", "list", "table", "inet", "KeenPbrTable").stdout


def baseline_config(backend: str) -> dict:
    config = json.loads(BASE_CONFIG.read_text(encoding="utf-8"))
    config["daemon"]["firewall_backend"] = backend
    return config


def cleanup_owned_state(context: SystemContext) -> None:
    context.run("systemctl", "stop", "keen-pbr.service", "dnsmasq.service", check=False)
    context.run("nft", "delete", "table", "inet", "KeenPbrTable", check=False)
    for binary in ("iptables", "ip6tables"):
        context.run(binary, "-t", "mangle", "-D", "PREROUTING", "-j", "KeenPbrTable",
                    check=False)
        context.run(binary, "-t", "mangle", "-F", "KeenPbrTable", check=False)
        context.run(binary, "-t", "mangle", "-X", "KeenPbrTable", check=False)
    saved = context.run("ipset", "save", check=False).stdout
    for line in saved.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "create" and parts[1].startswith(("kpbr4", "kpbr6")):
            context.run("ipset", "destroy", parts[1], check=False)
    # A killed daemon may not have reached its normal netlink cleanup. Remove
    # only policy rules/routes carrying Keen PBR's reserved protocol number.
    for family in ("-4", "-6"):
        rules = context.run("ip", family, "rule", "show", check=False).stdout
        for line in rules.splitlines():
            if not re.search(r"\b(proto|protocol) 186\b", line):
                continue
            priority = line.split(":", 1)[0].strip()
            if priority.isdigit():
                context.run("ip", family, "rule", "del", "pref", priority, check=False)
        context.run("ip", family, "route", "flush", "table", "all", "proto", "186",
                    check=False)
    context.run("conntrack", "-F", check=False)


def setup_case(context: SystemContext, _case) -> None:
    cleanup_owned_state(context)
    context.run("bash", str(TOPOLOGY), "reset")
    CONFIG_PATH.write_text(json.dumps(baseline_config(context.backend), indent=2) + "\n",
                           encoding="utf-8")
    context.run("systemctl", "reset-failed", "dnsmasq.service", "keen-pbr.service",
                check=False)
    context.run("systemctl", "restart", "dnsmasq.service")
    context.run("systemctl", "start", "keen-pbr.service")
    context.wait_for("keen-pbr service health", context.health_running)
    assert context.run("systemctl", "is-active", "dnsmasq.service").stdout.strip() == "active"


def teardown_case(context: SystemContext, _case) -> None:
    cleanup_owned_state(context)
    context.run("bash", str(TOPOLOGY), "cleanup", check=False)


def diagnostics(context: SystemContext, _case) -> str:
    commands = [
        ("systemctl", "--no-pager", "--full", "status", "keen-pbr.service", "dnsmasq.service"),
        ("journalctl", "--no-pager", "-n", "120", "-u", "keen-pbr.service", "-u", "dnsmasq.service"),
        ("ip", "rule", "show"), ("ip", "route", "show", "table", "all"),
    ]
    commands.append(("iptables-save", "-t", "mangle") if context.backend == "iptables"
                    else ("nft", "list", "table", "inet", "KeenPbrTable"))
    chunks = []
    for command in commands:
        result = context.run(*command, check=False)
        chunks.append(f"$ {' '.join(command)} (exit {result.returncode})\n{result.stdout}{result.stderr}")
    return "\n".join(chunks)


def preserve_diagnostic(case, text: str) -> None:
    path = pathlib.Path(os.environ.get("KPBR_IT_DIAGNOSTICS",
                                      "/mnt/seed/case-diagnostics.log"))
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"=== {case.name} ===\n{text}\n")


registry = Registry()


@registry.case("service_lifecycle")
def service_lifecycle(context: SystemContext) -> None:
    health = context.health_running()
    assert health["os_type"] == "debian", health
    routing = context.api("/api/health/routing")
    assert routing["overall"] == "ok", routing
    assert routing["firewall_backend"] == context.backend, routing
    stopped = context.api("/api/service/stop", "POST")
    assert stopped["status"] == "ok", stopped
    context.wait_for("stopped runtime",
                     lambda: context.api("/api/health/service")["status"] == "stopped")
    started = context.api("/api/service/start", "POST")
    assert started["status"] == "ok", started
    context.wait_for("restarted runtime", context.health_running)


@registry.case("dns_routing_save")
def dns_routing_save(context: SystemContext) -> None:
    context.wait_for("urltest selection", context.selected_outbound)
    context.resolve("routed.test")
    context.wait_for("dynamic routed set", context.dynamic_set_contains)
    context.assert_http_outbound("routed.test", "wan_pbr")
    context.resolve("direct.test", "198.18.0.11")
    context.assert_http_outbound("direct.test", "wan_direct", "198.18.0.11")
    before = context.run("systemctl", "show", "--value", "--property", "MainPID",
                         "dnsmasq.service").stdout.strip()
    config = context.api("/api/config")["config"]
    config["lists"]["routed"]["domains"].append("added.test")
    context.apply_config(config)
    context.wait_for("dnsmasq restart", lambda: context.run(
        "systemctl", "show", "--value", "--property", "MainPID", "dnsmasq.service"
    ).stdout.strip() not in ("", before))
    def converged():
        health = context.health_running()
        assert health.get("resolver_config_sync_state") == "converged", health
        assert health.get("resolver_config_hash") == health.get("resolver_config_hash_actual"), health
        return True
    context.wait_for("resolver convergence", converged)
    context.resolve("added.test")
    context.wait_for("saved domain set", context.dynamic_set_contains)
    context.assert_http_outbound("added.test", "wan_pbr")


@registry.case("urltest_rebuild")
def urltest_rebuild(context: SystemContext) -> None:
    context.wait_for("initial urltest selection", context.selected_outbound)
    context.api("/api/service/stop", "POST")
    context.wait_for("urltest runtime stop",
                     lambda: context.api("/api/health/service")["status"] == "stopped")
    context.api("/api/service/start", "POST")
    context.wait_for("urltest selection after routing rebuild", context.selected_outbound)
    context.resolve("routed.test")
    context.assert_http_outbound("routed.test", "wan_pbr")


def shape_config(context: SystemContext) -> dict:
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "wan", "type": "interface", "interface": "wan_direct", "gateway": "10.10.0.2"},
        {"tag": "block", "type": "blackhole"}, {"tag": "direct", "type": "ignore"},
    ]
    config["lists"] = {"hybrid": {"ip_cidrs": ["10.10.0.0/24", "2001:db8:10::/64"]}}
    config["route"]["rules"] = [
        {"list": ["hybrid"], "outbound": "wan", "proto": "tcp", "dest_port": "443"},
        {"list": ["hybrid"], "outbound": "block", "proto": "udp",
         "src_addr": "!10.0.0.0/8", "dest_port": "53"},
        {"outbound": "direct", "proto": "udp", "dest_addr": "8.8.8.8", "dest_port": "53"},
        {"outbound": "wan", "src_port": "1111"},
        {"outbound": "block", "proto": "tcp", "src_addr": "192.168.1.0/24",
         "dest_port": "!443"},
        {"outbound": "wan", "proto": "udp", "dest_addr": "2001:db8:53::53",
         "dest_port": "53"},
    ]
    return config


@registry.case("rule_shapes")
def rule_shapes(context: SystemContext) -> None:
    context.apply_config(shape_config(context))
    firewall = context.firewall_text()
    for token in ("443", "53", "1111"):
        assert token in firewall, f"missing {token} in firewall\n{firewall}"
    assert ("DROP" if context.backend == "iptables" else "drop") in firewall
    assert ("RETURN" if context.backend == "iptables" else "accept") in firewall
    assert "2001:db8:53::53" in firewall


@registry.case("table_interface")
def table_interface(context: SystemContext) -> None:
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "main", "type": "table", "table": 254},
        {"tag": "cloudflare", "type": "interface", "interface": "wan_pbr",
         "gateway": "10.20.0.2"},
    ]
    config["lists"] = {"table_targets": {"ip_cidrs": ["203.0.113.0/24"]}}
    config["route"] = {"inbound_interfaces": ["lan0"], "rules": [
        {"list": ["table_targets"], "outbound": "main"},
        {"outbound": "cloudflare", "proto": "tcp/udp", "dest_port": "443,80"},
    ]}
    context.apply_config(config)
    firewall = context.firewall_text()
    assert "lan0" in firewall, firewall
    assert "443" in firewall and "80" in firewall, firewall
    rules = context.run("ip", "rule", "show").stdout
    assert "lookup" in rules, rules


@registry.case("multiport_validation")
def multiport_validation(context: SystemContext) -> None:
    config = context.api("/api/config")["config"]
    config["outbounds"] = [{"tag": "wan", "type": "interface",
                            "interface": "wan_direct", "gateway": "10.10.0.2"}]
    config["lists"] = {}
    config["route"] = {"rules": [{"outbound": "wan", "proto": "tcp",
                                    "dest_addr": "198.51.100.2",
                                    "src_port": "555,666", "dest_port": "555-666"}]}
    if context.backend == "iptables":
        try:
            context.api("/api/config", "POST", config)
        except AssertionError as error:
            assert "xt_multiport" in str(error), error
            return
        raise AssertionError("iptables accepted the unsupported mixed multiport rule")
    context.apply_config(config)
    firewall = context.firewall_text()
    assert "555" in firewall and "666" in firewall, firewall


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("iptables", "nftables"):
        print("usage: test-system.py <iptables|nftables>", file=sys.stderr)
        return 2
    backend = sys.argv[1]
    try:
        cases = registry.select(os.environ.get("INTEGRATION_CASES", "all"), backend)
    except ValueError as error:
        print(f"KPBR_IT_END backend={backend} status=invalid_cases message={str(error).replace(' ', '_')}")
        return 2
    context = SystemContext(backend)
    timeout_seconds = float(os.environ.get("INTEGRATION_CASE_TIMEOUT", "180"))
    results = Runner(backend, cases, context, setup_case, teardown_case, diagnostics,
                     timeout_seconds=timeout_seconds,
                     preserve=preserve_diagnostic).run()
    summary_path = os.environ.get("KPBR_IT_SUMMARY", "/mnt/seed/summary.json")
    write_summary(summary_path, backend, results)
    return aggregate_status(results)


if __name__ == "__main__":
    raise SystemExit(main())
