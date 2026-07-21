def register(registry):
    @registry.case("dns_routing_save")
    def dns_routing_save(context):
        context.wait_for("urltest selection", context.selected_outbound)
        context.resolve("routed.test", "198.18.0.10")
        context.wait_for("dynamic routed set", context.dynamic_set_contains)
        context.assert_probe_path("wan_pbr", destination="198.18.0.10")
        context.resolve("direct.test", "198.18.0.10")
        context.assert_probe_path("wan_direct", destination="198.18.0.11")
        before = context.run("systemctl", "show", "--value", "--property", "MainPID",
                             "dnsmasq.service").stdout.strip()
        config = context.api("/api/config")["config"]
        config["lists"]["routed"]["domains"].append("added.test")
        context.apply_config(config)
        context.wait_for("dnsmasq restart", lambda: context.run(
            "systemctl", "show", "--value", "--property", "MainPID", "dnsmasq.service"
        ).stdout.strip() not in ("", before))

        def converged():
            health = context.health_running()
            assert health.get("resolver_config_sync_state") == "converged", health
            assert health.get("resolver_config_hash") == health.get("resolver_config_hash_actual"), health
            return True

        context.wait_for("resolver convergence", converged)
        context.resolve("added.test", "198.18.0.10")
        context.wait_for("saved domain set", context.dynamic_set_contains)
        context.assert_probe_path("wan_pbr", destination="198.18.0.10")
