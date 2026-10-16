def register(registry):
    @registry.case("dns_routing_save", requires=("nfqueue",))
    def dns_routing_save(context):
        context.wait_for("urltest selection", context.selected_outbound)
        context.resolve("routed.test", "198.18.0.10")
        context.wait_for("dynamic routed set", context.dynamic_set_contains)
        context.assert_probe_path("wan_pbr", destination="198.18.0.10")
        context.resolve("direct.test", "198.18.0.10")
        context.assert_probe_path("wan_direct", destination="198.18.0.11")
        config = context.api("/api/config")["config"]
        config["lists"]["routed"]["domains"].append("added.test")
        context.apply_config(config)
        context.health_running()
        context.resolve("added.test", "198.18.0.10")
        context.wait_for("saved domain set", context.dynamic_set_contains)
        context.assert_probe_path("wan_pbr", destination="198.18.0.10")
