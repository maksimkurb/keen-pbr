from .routing_common import apply, probe


def register(registry):
    @registry.case("route_proto")
    def route_proto(context):
        apply(context, [{"outbound": "wan_pbr", "proto": "tcp",
                         "dest_addr": "198.18.0.0/24"}])
        probe(context, "wan_pbr", proto="tcp")
        probe(context, "wan_direct", proto="udp")
