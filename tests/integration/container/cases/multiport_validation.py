def register(registry):
    @registry.case("multiport_validation")
    def multiport_validation(context):
        config = context.api("/api/config")["config"]
        config["outbounds"] = [{"tag": "wan", "type": "interface",
                                "interface": "wan_direct", "gateway": "10.10.0.2"}]
        config["lists"] = {}
        config["dns"]["rules"] = []
        config["route"] = {"rules": [{"outbound": "wan", "proto": "tcp",
                                        "dest_addr": "198.51.100.2",
                                        "src_port": "555,666", "dest_port": "555-666"}]}
        if context.backend == "iptables":
            try:
                context.api("/api/config", "POST", config)
            except AssertionError as error:
                assert "xt_multiport" in str(error), error
                return
            raise AssertionError("iptables accepted the unsupported mixed multiport rule")
        context.apply_config(config)
        firewall = context.firewall_text()
        assert "555" in firewall and "666" in firewall, firewall
