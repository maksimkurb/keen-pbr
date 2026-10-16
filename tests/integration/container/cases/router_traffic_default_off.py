from __future__ import annotations

from integration_context import TEST_IP

from .routing_common import apply, probe, router_probe

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
RULES = [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"}]


def register(registry):
    @registry.case("router_traffic_default_off")
    def router_traffic_default_off(context):
        # iproute.process_router_traffic defaults to false: the route rule
        # classifies forwarded LAN traffic but leaves the router's own traffic
        # on the main table (wan_direct).
        apply(context, RULES)
        probe(context, "wan_pbr", destination_port=19000)
        router_probe(context, "wan_direct", 19010)

        # Enabling the option routes the router's own traffic too.
        apply(context, RULES, router_traffic=True)
        probe(context, "wan_pbr", destination_port=19000)
        router_probe(context, "wan_pbr", 19011)

        # And disabling it again restores the default.
        apply(context, RULES)
        router_probe(context, "wan_direct", 19020)
