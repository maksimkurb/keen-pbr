from .routing_common import probe, routing_config


def register(registry):
    @registry.case("route_drop")
    def route_drop(context):
        config = routing_config(context, [
            {"outbound": "block", "proto": "tcp", "dest_port": "19020",
             "dest_addr": "198.18.0.10/32"},
            {"outbound": "wan_pbr", "proto": "tcp",
             "dest_addr": "198.18.0.10/32"},
        ])
        config["outbounds"].append({"tag": "block", "type": "blackhole"})
        context.apply_config(config)
        token = "route-drop-blackhole"
        context.run("conntrack", "-F", check=False)
        assert context.client_probe(destination_port=19020, token=token,
                                    check=False) is None
        for side in ("direct", "pbr"):
            assert not context.observations(side, tokens={token})

        probe(context, "wan_pbr", destination_port=19011,
              token="route-drop-pbr")
        assert context.observations("pbr", tokens={"route-drop-pbr"})
