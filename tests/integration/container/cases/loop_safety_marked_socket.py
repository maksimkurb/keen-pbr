from __future__ import annotations

from integration_context import TEST_IP

from .routing_common import apply, router_probe, routing_config

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
RULES = [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"}]
# A mark outside keen-pbr's fwmark mask (0x00ff0000) that no ip rule routes, like
# a WireGuard fwmark or an xray sockopt mark on a tunnel client's own socket.
CLIENT_MARK = 0x1


def register(registry):
    @registry.case("loop_safety_marked_socket")
    def loop_safety_marked_socket(context):
        # Router-originated traffic is classified: an unmarked probe is routed.
        apply(context, RULES)
        router_probe(context, "wan_pbr", 19010)

        # A tunnel client marks its own socket, so its encrypted packets must be
        # skipped by the prefilter and keep the main-table route (no loop).
        router_probe(context, "wan_direct", 19011, mark=CLIENT_MARK)

        # Negative control: without the skip-marked-packets prefilter the same
        # marked socket is captured into wan_pbr, so the protection is what
        # made the marked probe leave via wan_direct.
        config = routing_config(context, RULES)
        config["daemon"]["skip_marked_packets"] = False
        context.apply_config(config)
        router_probe(context, "wan_pbr", 19011, mark=CLIENT_MARK)

        # Restoring the default restores the protection.
        config = routing_config(context, RULES)
        config["daemon"]["skip_marked_packets"] = True
        context.apply_config(config)
        router_probe(context, "wan_direct", 19010, mark=CLIENT_MARK)
