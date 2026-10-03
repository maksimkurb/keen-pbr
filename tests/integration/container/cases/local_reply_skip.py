from __future__ import annotations

import subprocess
import time
import uuid

from integration_context import REMOTE_CONTAINER_DIR, parse_probe

from .routing_common import apply

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
PORT = 19600
ROUTER_LAN_IP = "192.0.2.1"
LOG = "/tmp/kpbr_it_local_reply.log"


def register(registry):
    @registry.case("local_reply_skip")
    def local_reply_skip(context):
        # iptables-save drops `-d 0.0.0.0/0` and nft merges adjacent halves,
        # either of which the firewall verifier reads back as drift, so each
        # backend gets the spelling it round-trips.
        catch_all = ("0.0.0.0/1,128.0.0.0/1" if context.backend == "iptables"
                     else "0.0.0.0/0")
        # A catch-all UDP route rule also applies to router-originated
        # packets. The answers a local service (think dnsmasq) sends to a LAN
        # client are conntrack REPLY packets and must keep following the main
        # table instead of being marked into the wan_pbr policy table.
        apply(context, [{"outbound": "wan_pbr", "proto": "udp",
                        "dest_addr": catch_all}])
        server = subprocess.Popen(
            ["python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "server",
             "--identity", "router", "--log", LOG, "--ports", str(PORT)],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            token = uuid.uuid4().hex
            result = None
            for _ in range(10):  # the listener starts asynchronously
                time.sleep(0.5)
                context.run("conntrack", "-F", check=False)
                result = context.client(
                    "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
                    "--proto", "udp", "--destination", ROUTER_LAN_IP,
                    "--destination-port", str(PORT), "--token", token,
                    "--timeout", "2", check=False, timeout=10)
                if result.returncode == 0:
                    break
            assert result.returncode == 0, (
                "LAN client got no answer from the local service: "
                f"{result.stderr}")
            assert parse_probe(result.stdout, token)["identity"] == "router"
        finally:
            server.kill()
            server.wait()
            context.run("conntrack", "-F", check=False)
