from .routing_common import apply, probe


def register(registry):
    @registry.case("route_dest_port")
    def route_dest_port(context):
        apply(context, [{"outbound": "wan_pbr", "proto": "tcp",
                         "dest_port": "19010", "dest_addr": "198.18.0.0/24"}])
        probe(context, "wan_pbr", destination_port=19010)
        probe(context, "wan_direct", destination_port=19011)
