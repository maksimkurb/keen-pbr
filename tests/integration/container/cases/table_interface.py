def register(registry):
    @registry.case("table_interface")
    def table_interface(context):
        config = context.api("/api/config")["config"]
        config["outbounds"] = [
            {"tag": "main", "type": "table", "table": 254},
            {"tag": "cloudflare", "type": "interface", "interface": "wan_pbr",
             "gateway": "10.20.0.2"},
        ]
        config["lists"] = {"table_targets": {"ip_cidrs": ["203.0.113.0/24"]}}
        config["dns"]["rules"] = []
        config["route"] = {"inbound_interfaces": ["lan0"], "rules": [
            {"list": ["table_targets"], "outbound": "main"},
            {"outbound": "cloudflare", "proto": "tcp/udp", "dest_port": "443,80"},
        ]}
        context.apply_config(config)
        firewall = context.firewall_text()
        assert "lan0" in firewall, firewall
        assert "443" in firewall and "80" in firewall, firewall
        assert "lookup" in context.run("ip", "rule", "show").stdout
