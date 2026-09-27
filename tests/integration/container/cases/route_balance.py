def register(registry):
    @registry.case("route_balance", backends=("nftables",))
    def route_balance(context):
        config = context.api("/api/config")["config"]
        config["outbounds"] = [
            {"tag": "wan_direct", "type": "interface", "interface": "wan_direct",
             "gateway": "10.10.0.2", "gateway6": "2001:db8:10::2"},
            {"tag": "wan_pbr", "type": "interface", "interface": "wan_pbr",
             "gateway": "10.20.0.2", "gateway6": "2001:db8:20::2"},
            {"tag": "auto", "type": "urltest",
             "url": "http://198.18.0.10:18080/health", "interval_ms": 1000,
             "probe_timeout_ms": 500, "strategy": "balance",
             "conntrack_on_switch": "preserve",
             "retry": {"attempts": 1, "interval_ms": 100},
             "outbound_groups": [{"weight": 1,
                                  "outbounds": ["wan_direct", "wan_pbr"]}]},
        ]
        config["lists"] = {}
        config["dns"]["rules"] = []
        config["route"] = {"inbound_interfaces": ["lan0"], "rules": [
            {"outbound": "auto", "dest_addr": "198.18.0.10/32"},
        ]}
        context.apply_config(config)

        def both_candidates_active():
            state = context.api("/api/runtime/outbounds")
            automatic = next(item for item in state["outbounds"]
                             if item["tag"] == "auto")
            candidates = {item["outbound_tag"]: item
                          for item in automatic.get("interfaces", [])}
            if set(candidates) != {"wan_direct", "wan_pbr"}:
                return False
            return (state if all(candidates[tag]["status"] == "active"
                                 for tag in candidates) else False)

        context.wait_for("both balance candidates active", both_candidates_active)

        routing = context.routing_health_running()
        assert routing["firewall_backend"] == "nftables", routing
        assert routing["overall"] == "ok", routing
        firewall = context.firewall_text()
        assert "numgen" in firewall and "vmap" in firewall, firewall

        identities = set()
        for index in range(4):
            payload = context.client_probe(
                destination="198.18.0.10", destination_port=19000,
                source="192.0.2.2", source_port=22000 + index,
                token=f"route-balance-{index}")
            assert payload is not None and payload["identity"] in {"wan_direct", "wan_pbr"}, payload
            identities.add(payload["identity"])

        assert identities == {"wan_direct", "wan_pbr"}, identities
