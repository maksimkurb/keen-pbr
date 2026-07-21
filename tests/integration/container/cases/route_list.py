from .routing_common import apply, probe


def register(registry):
    @registry.case("route_list")
    def route_list(context):
        apply(context, [{"list": ["targets"], "outbound": "wan_pbr"}], {
            "targets": {"ip_cidrs": ["198.18.0.10/32", "2001:db8:100::10/128"]}})
        probe(context, "wan_pbr", destination="198.18.0.10")
        probe(context, "wan_direct", destination="198.18.0.11")
        probe(context, "wan_pbr", destination="2001:db8:100::10")
        probe(context, "wan_direct", destination="2001:db8:100::11")
