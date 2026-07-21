from .routing_common import apply, probe


def register(registry):
    @registry.case("route_dest_addr")
    def route_dest_addr(context):
        apply(context, [
            {"outbound": "wan_pbr", "dest_addr": "198.18.0.10/32"},
            {"outbound": "wan_pbr", "dest_addr": "2001:db8:100::10/128"},
        ])
        probe(context, "wan_pbr", destination="198.18.0.10")
        probe(context, "wan_direct", destination="198.18.0.11")
        probe(context, "wan_pbr", destination="2001:db8:100::10")
        probe(context, "wan_direct", destination="2001:db8:100::11")
