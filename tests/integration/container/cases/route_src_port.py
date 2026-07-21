from .routing_common import apply, probe


def register(registry):
    @registry.case("route_src_port")
    def route_src_port(context):
        apply(context, [{"outbound": "wan_pbr", "proto": "tcp",
                         "src_port": "20000", "dest_addr": "198.18.0.0/24"}])
        probe(context, "wan_pbr", source_port=20000)
        probe(context, "wan_direct", source_port=20001)
