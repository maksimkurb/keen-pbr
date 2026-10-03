from __future__ import annotations

import uuid

from integration_context import REMOTE_CONTAINER_DIR, TEST_IP, parse_probe

from .routing_common import apply, probe, routing_config

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
RULES = [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"}]
# The pbr WAN host only routes back to the LAN. A router-originated packet
# re-routed into wan_pbr keeps the wan_direct source chosen by the main table.
RETURN_ROUTE = ("10.10.0.0/24", "via", "10.20.0.1", "dev", "wan_pbr")


def _router_probe(context, expected, destination_port):
    """Probe from the router itself (OUTPUT path, no input interface)."""
    token = uuid.uuid4().hex
    context.wan("pbr", "ip", "route", "replace", *RETURN_ROUTE)
    try:
        context.run("conntrack", "-F", check=False)
        result = context.run(
            "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
            "--proto", "tcp", "--destination", TEST_IP,
            "--destination-port", str(destination_port), "--token", token,
            "--timeout", "4", check=False, timeout=10)
        assert result.returncode == 0, result.stderr
        payload = parse_probe(result.stdout, token)
        assert payload["identity"] == expected, payload
    finally:
        context.wan("pbr", "ip", "route", "delete", *RETURN_ROUTE, check=False)
        context.run("conntrack", "-F", check=False)


def register(registry):
    @registry.case("inbound_interface_filter")
    def inbound_interface_filter(context):
        # Forwarded traffic from an allowed interface is classified, and router
        # traffic is classified too.
        apply(context, RULES)
        probe(context, "wan_pbr", destination_port=19000)
        _router_probe(context, "wan_pbr", 19010)

        # Forwarded traffic from a non-allowed interface is not classified.
        # Router traffic has no input interface, so the filter never applies
        # to it (guards `! -i <iface> RETURN` leaking into OUTPUT).
        config = routing_config(context, RULES)
        config["route"]["inbound_interfaces"] = ["wan_direct"]
        context.apply_config(config)
        probe(context, "wan_direct", destination_port=19020)
        _router_probe(context, "wan_pbr", 19011)
