from .routing_common import apply, probe


def register(registry):
    @registry.case("route_src_addr")
    def route_src_addr(context):
        apply(context, [
            {"outbound": "wan_pbr", "src_addr": "192.0.2.2/32",
             "dest_addr": "198.18.0.0/24"},
            {"outbound": "wan_pbr", "src_addr": "2001:db8:1::2/128",
             "dest_addr": "2001:db8:100::/64"},
        ])
        probe(context, "wan_pbr", source="192.0.2.2", destination="198.18.0.10")
        probe(context, "wan_direct", source="192.0.2.3", destination="198.18.0.11")
        probe(context, "wan_pbr", source="2001:db8:1::2", destination="2001:db8:100::10")
        probe(context, "wan_direct", source="2001:db8:1::3", destination="2001:db8:100::11")
