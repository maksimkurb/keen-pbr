from .routing_common import apply, probe


def register(registry):
    @registry.case("route_dscp")
    def route_dscp(context):
        apply(context, [{"outbound": "wan_pbr", "dscp": 46,
                         "dest_addr": "198.18.0.0/24"}])
        probe(context, "wan_pbr", dscp=46)
        probe(context, "wan_direct", dscp=0)
