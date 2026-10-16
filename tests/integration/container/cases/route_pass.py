from .routing_common import probe, routing_config


def register(registry):
    @registry.case("route_pass")
    def route_pass(context):
        config = routing_config(context, [
            {"outbound": "direct", "proto": "tcp", "dest_port": "19020",
             "dest_addr": "198.18.0.10/32"},
            {"outbound": "wan_pbr", "proto": "tcp",
             "dest_addr": "198.18.0.10/32"},
        ])
        config["outbounds"].append({"tag": "direct", "type": "ignore"})
        context.apply_config(config)
        probe(context, "wan_direct", destination_port=19020,
              token="route-pass-direct")
        probe(context, "wan_pbr", destination_port=19011,
              token="route-pass-pbr")

        assert context.observations("direct", tokens={"route-pass-direct"})
        assert context.observations("pbr", tokens={"route-pass-pbr"})
