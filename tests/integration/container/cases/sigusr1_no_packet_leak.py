from __future__ import annotations

import json
import time

from .routing_common import apply


def run_signal_cycles(cycles, send_signal, wait_complete, assert_state):
    """Sequence signals without overlapping the daemon's debounce window."""
    for index in range(cycles):
        assert_state(index, "before")
        sent_at = send_signal(index)
        wait_complete(index, sent_at)
        assert_state(index, "after")


def register(registry):
    @registry.case("sigusr1_no_packet_leak")
    def sigusr1_no_packet_leak(context):
        apply(context, [{"outbound": "wan_pbr", "proto": "tcp",
                         "dest_addr": "198.18.0.10/32"}])
        interfaces = ("lan0", "wan_direct", "wan_pbr")

        def state():
            return {name: json.loads(context.run("ip", "-j", "link", "show", "dev", name).stdout)[0]
                    for name in interfaces}

        baseline = state()
        command = (
            "rm -f /run/kpbr-probe-stream.jsonl; "
            "nohup python3 /mnt/payload/tests/integration/container/probe.py stream "
            "--proto tcp --destination 198.18.0.10 --destination-port 19020 "
            "--token sigusr1-stream --timeout 2 --interval 0.05 "
            "--output /run/kpbr-probe-stream.jsonl "
            ">/run/kpbr-probe-stream.launch.log 2>&1 &"
        )
        context.client("sh", "-c", command)
        context.wait_for("initial continuous client flow", lambda: len(
            context.client("cat", "/run/kpbr-probe-stream.jsonl", check=False).stdout.splitlines()) >= 3,
                         timeout=15)

        def assert_state(_index, _stage):
            current = state()
            for name in interfaces:
                assert current[name]["flags"] == baseline[name]["flags"], (baseline, current)
                assert current[name]["operstate"] == baseline[name]["operstate"], (baseline, current)

        def send_signal(_index):
            sent = time.time()
            context.run("systemctl", "kill", "-s", "SIGUSR1", "keen-pbr.service")
            return sent

        def wait_complete(_index, sent_at):
            def completed():
                output = context.run(
                    "journalctl", "--no-pager", "-u", "keen-pbr.service",
                    f"--since=@{sent_at:.3f}", "-g", "SIGUSR1: firewall refresh complete",
                    check=False).stdout
                return "SIGUSR1: firewall refresh complete" in output
            context.wait_for("SIGUSR1 firewall refresh completion", completed, timeout=25)

        run_signal_cycles(3, send_signal, wait_complete, assert_state)
        time.sleep(0.5)
        context.client("sh", "-c", "pkill -f '[p]robe.py stream' || true")
        values = [json.loads(line) for line in context.client(
            "cat", "/run/kpbr-probe-stream.jsonl").stdout.splitlines() if line.strip()]
        assert len(values) >= 10, values
        assert all(value.get("ok") and value.get("identity") == "wan_pbr" for value in values), values
        tokens = {value["token"] for value in values}
        leaked = context.observations("direct", tokens=tokens)
        assert not leaked, leaked
