from __future__ import annotations

import time
import uuid

from integration_context import CLIENT, REMOTE_CONTAINER_DIR, TEST_IP, parse_probe

from .routing_common import RETURN_ROUTE, apply

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
LAN_PORT = 6868
WAN_PORT = 19010
LAN_BROADCAST = "192.0.2.255"
LOG = "/tmp/kpbr_it_output_lan.log"

# One datagram from the router with no prior conntrack entry.
SENDER = (
    "import socket,sys;"
    "s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);"
    "s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);"
    f"s.sendto(sys.argv[2].encode(),(sys.argv[1],{LAN_PORT}))")


def _received(context) -> str:
    return context.client("cat", LOG, check=False).stdout


def _send_and_expect(context, destination: str, what: str) -> None:
    token = uuid.uuid4().hex
    context.run("conntrack", "-F", check=False)
    for _ in range(10):  # the listener starts asynchronously
        context.run("python3", "-c", SENDER, destination, token)
        time.sleep(0.5)
        # probe.py logs non-JSON datagrams as raw-udp with a hex payload.
        if token.encode().hex() in _received(context):
            return
    raise AssertionError(
        f"{what}: the LAN client never received the router's datagram "
        f"(it was policy-routed away); got: {_received(context)!r}")


def register(registry):
    @registry.case("output_lan_skip")
    def output_lan_skip(context):
        # A positive UDP catch-all applies to router-originated packets too.
        # Packets that start a NEW conntrack entry but already leave through a
        # LAN interface (a DHCP offer, RA, mDNS, unicast to a LAN host) are not
        # conntrack replies, so only the OUTPUT oif/broadcast/multicast skip
        # keeps them on the main table.
        apply(context, [{"outbound": "wan_pbr", "proto": "udp",
                         "dest_addr": "0.0.0.0/0"}])
        context.client("rm", "-f", LOG)
        context.client(
            "sh", "-c",
            f"nohup python3 {REMOTE_CONTAINER_DIR}/probe.py server "
            f"--identity lan_client --log {LOG} --ports {LAN_PORT} "
            ">/dev/null 2>&1 </dev/null &")
        try:
            _send_and_expect(context, CLIENT, "unicast to a LAN host")
            _send_and_expect(context, LAN_BROADCAST, "LAN broadcast")

            # Router -> internet UDP is still policy-routed by the rule.
            token = uuid.uuid4().hex
            context.wan("pbr", "ip", "route", "replace", *RETURN_ROUTE)
            try:
                context.run("conntrack", "-F", check=False)
                result = context.run(
                    "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
                    "--proto", "udp", "--destination", TEST_IP,
                    "--destination-port", str(WAN_PORT), "--token", token,
                    "--timeout", "4", check=False, timeout=10)
                assert result.returncode == 0, result.stderr
                payload = parse_probe(result.stdout, token)
                assert payload["identity"] == "wan_pbr", payload
            finally:
                context.wan("pbr", "ip", "route", "delete", *RETURN_ROUTE,
                            check=False)
        finally:
            context.client("pkill", "-f", f"[p]robe.py server.*{LOG}",
                           check=False)
            context.client("rm", "-f", LOG, check=False)
            context.run("conntrack", "-F", check=False)
