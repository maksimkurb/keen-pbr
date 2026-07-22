#!/usr/bin/env python3
"""Shared router control-plane and remote fixture helpers."""

from __future__ import annotations

import json
import os
import pathlib
import re
import shlex
import subprocess
import time
import urllib.error
import urllib.request
import uuid

API = "http://127.0.0.1:12121"
CLIENT = "192.0.2.2"
WAN_HOSTS = {"direct": "10.10.0.2", "pbr": "10.20.0.2"}
TEST_IP = "198.18.0.10"
TEST_IP6 = "2001:db8:100::10"
CONTAINER_DIR = pathlib.Path(__file__).resolve().parent
BASE_CONFIG = CONTAINER_DIR / "config.json"
TOPOLOGY = CONTAINER_DIR / "topology.sh"
CONFIG_PATH = pathlib.Path("/etc/keen-pbr/config.json")
REMOTE_CONTAINER_DIR = "/mnt/payload/tests/integration/container"


def ssh_command(key: str, host: str, arguments: tuple[str, ...]) -> tuple[str, ...]:
    command = shlex.join(arguments)
    return ("ssh", "-i", key, "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
            "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
            f"root@{host}", command)


def parse_probe(output: str, token: str) -> dict:
    lines = [line for line in output.splitlines() if line.strip()]
    if not lines:
        raise AssertionError("probe returned no output")
    payload = json.loads(lines[-1])
    if payload.get("token") != token:
        raise AssertionError(f"probe token mismatch: {payload!r}")
    return payload


def parse_observations(output: str, tokens: set[str] | None = None) -> list[dict]:
    values = [json.loads(line) for line in output.splitlines() if line.strip()]
    if tokens is None:
        return values
    return [value for value in values if value.get("token") in tokens]


class SystemContext:
    def __init__(self, backend: str, ssh_key: str | None = None) -> None:
        self.backend = backend
        self.ssh_key = ssh_key or os.environ.get("KPBR_SSH_KEY", "/root/.ssh/kpbr-integration")

    def run(self, *args: str, check: bool = True, timeout: int | float = 30):
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=timeout)
        if check and result.returncode:
            raise AssertionError(
                f"command failed ({' '.join(args)}, exit {result.returncode}): "
                f"{result.stderr.strip()}")
        return result

    def remote(self, host: str, *args: str, check: bool = True,
               timeout: int | float = 30):
        return self.run(*ssh_command(self.ssh_key, host, tuple(args)),
                        check=check, timeout=timeout)

    def client(self, *args: str, **kwargs):
        return self.remote(CLIENT, *args, **kwargs)

    def wan(self, side: str, *args: str, **kwargs):
        return self.remote(WAN_HOSTS[side], *args, **kwargs)

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

    def wait_for(self, description, predicate, timeout: float = 35,
                 interval: float = 0.25):
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
            time.sleep(interval)
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

    def resolve(self, domain: str, expected: tuple[str, ...] | str,
                record_type: str = "A", expect_success: bool = True):
        result = self.client("dig", "+time=2", "+tries=1", "+short", record_type,
                             domain, "@192.0.2.1", check=False, timeout=8)
        if not expect_success:
            assert result.returncode != 0 or not result.stdout.strip(), result.stdout
            return result
        assert result.returncode == 0, result.stderr
        wanted = (expected,) if isinstance(expected, str) else expected
        answers = set(result.stdout.split())
        assert set(wanted) <= answers, (wanted, answers)
        return result

    def client_probe(self, *, proto: str = "tcp", destination: str = TEST_IP,
                     destination_port: int = 19000, source: str | None = None,
                     source_port: int | None = None, dscp: int | None = None,
                     token: str | None = None, check: bool = True) -> dict | None:
        token = token or uuid.uuid4().hex
        args = ["python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
                "--proto", proto, "--destination", destination,
                "--destination-port", str(destination_port), "--token", token,
                "--timeout", "4"]
        if source is not None:
            args += ["--source", source]
        if source_port is not None:
            args += ["--source-port", str(source_port)]
        if dscp is not None:
            args += ["--dscp", str(dscp)]
        result = self.client(*args, check=False, timeout=10)
        if not check:
            return None if result.returncode else parse_probe(result.stdout, token)
        assert result.returncode == 0, result.stderr
        return parse_probe(result.stdout, token)

    def assert_probe_path(self, expected: str, **probe) -> dict:
        payload = self.client_probe(**probe)
        assert payload is not None and payload["identity"] == expected, payload
        return payload

    def observations(self, side: str, kind: str = "probe",
                     tokens: set[str] | None = None) -> list[dict]:
        path = {"probe": f"/run/kpbr-wan/{side}/observations.jsonl",
                "dns4": f"/run/kpbr-wan/{side}/dns-v4.jsonl",
                "dns6": f"/run/kpbr-wan/{side}/dns-v6.jsonl"}[kind]
        result = self.wan(side, "cat", path, check=False)
        return parse_observations(result.stdout, tokens)

    def reset_peers(self) -> None:
        self.client("sh", "-c",
                    "pkill -f '[p]robe.py stream' || true; "
                    "rm -f /run/kpbr-probe-stream.jsonl; "
                    "ip address replace 192.0.2.3/24 dev lan0; "
                    "ip -6 address replace 2001:db8:1::3/64 dev lan0")
        for side in WAN_HOSTS:
            self.wan(side, "sh", "-c",
                     f": > /run/kpbr-wan/{side}/observations.jsonl; "
                     f": > /run/kpbr-wan/{side}/dns-v4.jsonl; "
                     f": > /run/kpbr-wan/{side}/dns-v6.jsonl; "
                     "iptables -F 2>/dev/null || true; ip6tables -F 2>/dev/null || true")

    def dynamic_set_contains(self, address: str = TEST_IP):
        if self.backend == "iptables":
            family = "kpbr6d_routed" if ":" in address else "kpbr4d_routed"
            self.run("ipset", "test", family, address)
        else:
            family = "kpbr6d_routed" if ":" in address else "kpbr4d_routed"
            output = self.run("nft", "list", "set", "inet", "KeenPbrTable", family).stdout
            assert address in output, output
        return True

    def apply_config(self, config: dict):
        staged = self.api("/api/config", "POST", config)
        assert staged["status"] == "ok", staged
        saved = self.api("/api/config/save", "POST")
        assert saved["status"] == "accepted" and saved["operation_id"], saved
        def applied_operation():
            health = self.api("/api/health/service")
            operation = health.get("lifecycle_operation", {})
            return health if (operation.get("id") == saved["operation_id"] and
                              operation.get("status") == "succeeded") else False

        health = self.wait_for("config lifecycle completion", applied_operation)
        self.health_running()
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
    context.reset_peers()
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
    context.reset_peers()


def diagnostics(context: SystemContext, _case) -> str:
    commands = [
        ("systemctl", "--no-pager", "--full", "status", "keen-pbr.service", "dnsmasq.service"),
        ("journalctl", "--no-pager", "-n", "160", "-u", "keen-pbr.service", "-u", "dnsmasq.service"),
        ("ip", "-4", "rule", "show"), ("ip", "-6", "rule", "show"),
        ("ip", "-4", "route", "show", "table", "all"),
        ("ip", "-6", "route", "show", "table", "all"),
    ]
    commands.append(("iptables-save", "-t", "mangle") if context.backend == "iptables"
                    else ("nft", "list", "table", "inet", "KeenPbrTable"))
    chunks = []
    for command in commands:
        result = context.run(*command, check=False)
        chunks.append(f"$ {' '.join(command)} (exit {result.returncode})\n{result.stdout}{result.stderr}")
    for label, host in (("client", CLIENT), ("direct", WAN_HOSTS["direct"]),
                        ("pbr", WAN_HOSTS["pbr"])):
        result = context.remote(host, "sh", "-c", "ip address; ip route; ip -6 route; ps aux",
                                check=False)
        chunks.append(f"$ ssh {label} diagnostics (exit {result.returncode})\n{result.stdout}{result.stderr}")
    return "\n".join(chunks)


def preserve_diagnostic(case, text: str) -> None:
    path = pathlib.Path(os.environ.get("KPBR_IT_DIAGNOSTICS",
                                      "/mnt/seed/case-diagnostics.log"))
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"=== {case.name} ===\n{text}\n")
