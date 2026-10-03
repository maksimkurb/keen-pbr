from __future__ import annotations

from integration_context import TEST_IP

from .routing_common import apply, probe, router_probe, routing_config

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
RULES = [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"}]


def register(registry):
    @registry.case("inbound_interface_filter")
    def inbound_interface_filter(context):
        # Forwarded traffic from an allowed interface is classified, and router
        # traffic is classified too.
        apply(context, RULES)
        probe(context, "wan_pbr", destination_port=19000)
        router_probe(context, "wan_pbr", 19010)

        # Forwarded traffic from a non-allowed interface is not classified.
        # Router traffic has no input interface, so the filter never applies
        # to it (guards `! -i <iface> RETURN` leaking into OUTPUT).
        config = routing_config(context, RULES)
        config["route"]["inbound_interfaces"] = ["wan_direct"]
        context.apply_config(config)
        probe(context, "wan_direct", destination_port=19020)
        router_probe(context, "wan_pbr", 19011)
