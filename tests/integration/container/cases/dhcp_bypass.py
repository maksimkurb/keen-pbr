from __future__ import annotations

import re
import time
import uuid

from integration_context import CLIENT, REMOTE_CONTAINER_DIR, TEST_IP, TEST_IP6, parse_probe

from .routing_common import RETURN_ROUTE, routing_config

V4_LOG = "/tmp/kpbr_it_dhcp_v4.log"
V6_LOG = "/tmp/kpbr_it_dhcp_v6.log"
LAN_LOG = "/tmp/kpbr_it_dhcp_lan.log"


def _start_server(context, side: str, log: str, ports: str) -> None:
    context.wan(side, "rm", "-f", log)
    context.wan(
        side,
        "sh",
        "-c",
        f"nohup python3 {REMOTE_CONTAINER_DIR}/probe.py server "
        f"--identity wan_{side} --log {log} --ports {ports} --reply-from-destination "
        ">/dev/null 2>&1 </dev/null &",
    )


def _udp_probe(context, destination: str, destination_port: int,
               source_port: int, expected: str) -> None:
    context.run("conntrack", "-F", check=False)
    result = context.client_probe(
        proto="udp", destination=destination, destination_port=destination_port,
        source_port=source_port, check=False)
    assert result is not None and result["identity"] == expected, result


def _router_udp_probe(context, destination: str, destination_port: int,
                      source_port: int, expected: str) -> None:
    token = uuid.uuid4().hex
    context.run("conntrack", "-F", check=False)
    result = context.run(
        "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
        "--proto", "udp", "--destination", destination,
        "--destination-port", str(destination_port), "--source-port",
        str(source_port), "--token", token, "--timeout", "4",
        check=False, timeout=10)
    assert result.returncode == 0, result.stderr
    payload = parse_probe(result.stdout, token)
    assert payload["identity"] == expected, payload


def _broadcast(context) -> None:
    token = uuid.uuid4().hex
    sender = (
        "import json,socket,sys;"
        "s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);"
        "s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);"
        "s.bind(('0.0.0.0',67));"
        "s.sendto(json.dumps({'token':sys.argv[1]}).encode(),"
        "('192.0.2.255',68))"
    )
    context.run("python3", "-c", sender, token)
    for _ in range(10):
        if token in context.client("cat", LAN_LOG, check=False).stdout:
            return
        time.sleep(0.25)
    raise AssertionError("DHCP broadcast did not reach the LAN listener")


def register(registry):
    @registry.case("dhcp_bypass")
    def dhcp_bypass(context):
        _start_server(context, "direct", V4_LOG, "67")
        _start_server(context, "pbr", V4_LOG, "67")
        _start_server(context, "direct", V6_LOG, "547")
        _start_server(context, "pbr", V6_LOG, "547")
        context.client("rm", "-f", LAN_LOG)
        context.client(
            "sh", "-c",
            f"nohup python3 {REMOTE_CONTAINER_DIR}/probe.py server "
            f"--identity lan --log {LAN_LOG} --ports 68 "
            ">/dev/null 2>&1 </dev/null &",
        )
        context.wan("pbr", "ip", "route", "replace", *RETURN_ROUTE)
        try:
            config = routing_config(
                context, [{"outbound": "wan_pbr", "proto": "udp",
                           "dest_port": "1-65535"}],
                router_traffic=True)
            # DHCP must remain safe even when the optional interface allowlist
            # is absent and all UDP is otherwise policy-routed.
            config["route"]["inbound_interfaces"] = []
            context.apply_config(config)

            _udp_probe(context, TEST_IP, 67, 68, "wan_direct")
            _udp_probe(context, TEST_IP, 67, 67, "wan_direct")
            _udp_probe(context, TEST_IP6, 547, 546, "wan_direct")
            _udp_probe(context, TEST_IP6, 547, 547, "wan_direct")
            _router_udp_probe(context, TEST_IP, 67, 67, "wan_direct")
            _broadcast(context)

            # Both ports and the IP family must match the DHCP bypass.
            _udp_probe(context, TEST_IP, 67, 41010, "wan_pbr")
            connection = context.run(
                "conntrack", "-L", "-p", "udp", "--orig-src", CLIENT,
                "--orig-dst", TEST_IP, "--sport", "41010", "--dport", "67")
            saved_mark = int(re.search(r"mark=(\d+)", connection.stdout).group(1))
            assert saved_mark != 0, connection.stdout

            # Old DHCP connmarks must not restore the previously chosen route.
            _udp_probe(context, TEST_IP, 67, 68, "wan_direct")
            context.run(
                "conntrack", "-U", "-p", "udp", "--orig-src", CLIENT,
                "--orig-dst", TEST_IP, "--sport", "68", "--dport", "67",
                "--mark", str(saved_mark))
            result = context.client_probe(
                proto="udp", destination=TEST_IP, destination_port=67, source_port=68)
            assert result["identity"] == "wan_direct", result
            _udp_probe(context, TEST_IP, 547, 67, "wan_pbr")
        finally:
            context.wan("pbr", "ip", "route", "delete", *RETURN_ROUTE,
                        check=False)
            context.run("conntrack", "-F", check=False)
            for side in ("direct", "pbr"):
                context.wan(side, "pkill", "-f", "[p]robe.py server.*kpbr_it_dhcp",
                            check=False)
            context.client("pkill", "-f", "[p]robe.py server.*kpbr_it_dhcp",
                           check=False)
