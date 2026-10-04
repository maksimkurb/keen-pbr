from __future__ import annotations

import re

from integration_context import REMOTE_CONTAINER_DIR, TEST_IP

from .routing_common import routing_config

# The shared WAN fixtures answer UDP from their egress address, which conntrack
# sees as a new flow and not as a reply. This case starts its own pbr-side
# server that answers from the address it was queried on, like a real server.
PORT = 19030
SOURCE_PORT = 41111
LOG = "/tmp/kpbr_it_forwarded_reply.log"


def _reply_skip_packets(context) -> int:
    """Packets counted by the PREROUTING reply-direction skip rule."""
    if context.backend == "iptables":
        listing = context.run("iptables", "-t", "mangle", "-vnxL",
                              "KeenPbrTable").stdout
        pattern = r"^\s*(\d+)\s+\d+\s+RETURN\b.*ctdir REPLY"
    else:
        listing = context.run("nft", "list", "chain", "inet", "KeenPbrTable",
                              "prerouting").stdout
        pattern = r"ct direction reply counter packets (\d+)"
    match = re.search(pattern, listing, re.MULTILINE)
    assert match, f"reply-direction skip rule missing in PREROUTING:\n{listing}"
    return int(match.group(1))


def register(registry):
    @registry.case("forwarded_reply_skip")
    def forwarded_reply_skip(context):
        # With no route.inbound_interfaces a catch-all route rule sees the
        # forwarded REPLY packets (WAN server -> LAN client) too. They must be
        # left alone instead of being re-marked into the policy table, which
        # would send them straight back out of the tunnel interface.
        config = routing_config(context, [
            {"outbound": "wan_pbr", "proto": "udp", "dest_addr": "0.0.0.0/0"}])
        config["route"]["inbound_interfaces"] = []
        try:
            context.wan(
                "pbr", "sh", "-c",
                f"nohup python3 {REMOTE_CONTAINER_DIR}/probe.py server "
                f"--identity wan_pbr --log {LOG} --ports {PORT} "
                "--reply-from-destination >/dev/null 2>&1 </dev/null &")
            context.apply_config(config)
            context.run("conntrack", "-F", check=False)
            before = _reply_skip_packets(context)
            payload = None
            for _ in range(10):  # the listener starts asynchronously
                payload = context.client_probe(
                    proto="udp", destination=TEST_IP, destination_port=PORT,
                    source_port=SOURCE_PORT, check=False)
                if payload is not None:
                    break
                context.run("conntrack", "-F", check=False)
            assert payload is not None, "LAN client got no reply"
            # The request left through the policy outbound and its reply made
            # it back to the LAN client.
            assert payload["identity"] == "wan_pbr", payload
            # The reply was seen and skipped by the PREROUTING classifier.
            after = _reply_skip_packets(context)
            assert after > before, (before, after)
            # The flow's connection mark was set by the original direction
            # only (a re-marked reply would still show the same value, so the
            # counter above is the real check).
            flow = context.run("conntrack", "-L", "-p", "udp", "--dport",
                               str(PORT), check=False).stdout
            assert re.search(r"mark=[1-9]\d*", flow), flow
        finally:
            context.wan("pbr", "pkill", "-f", "probe.py server --identity wan_pbr --log "
                        + LOG, check=False)
            context.run("conntrack", "-F", check=False)
