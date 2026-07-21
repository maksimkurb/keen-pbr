def shape_config(context):
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "wan", "type": "interface", "interface": "wan_direct", "gateway": "10.10.0.2"},
        {"tag": "block", "type": "blackhole"}, {"tag": "direct", "type": "ignore"},
    ]
    config["lists"] = {"hybrid": {"ip_cidrs": ["10.10.0.0/24", "2001:db8:10::/64"]}}
    config["dns"]["rules"] = []
    config["route"]["rules"] = [
        {"list": ["hybrid"], "outbound": "wan", "proto": "tcp", "dest_port": "443"},
        {"list": ["hybrid"], "outbound": "block", "proto": "udp",
         "src_addr": "!10.0.0.0/8", "dest_port": "53"},
        {"outbound": "direct", "proto": "udp", "dest_addr": "8.8.8.8", "dest_port": "53"},
        {"outbound": "wan", "src_port": "1111"},
        {"outbound": "block", "proto": "tcp", "src_addr": "192.168.1.0/24",
         "dest_port": "!443"},
        {"outbound": "wan", "proto": "udp", "dest_addr": "2001:db8:53::53",
         "dest_port": "53"},
    ]
    return config


def register(registry):
    @registry.case("rule_shapes")
    def rule_shapes(context):
        context.apply_config(shape_config(context))
        firewall = context.firewall_text()
        for token in ("443", "53", "1111"):
            assert token in firewall, f"missing {token} in firewall\n{firewall}"
        assert ("DROP" if context.backend == "iptables" else "drop") in firewall
        assert ("RETURN" if context.backend == "iptables" else "accept") in firewall
        assert "2001:db8:53::53" in firewall
