def stage_ids(operation):
    return [stage["id"] for stage in operation["stages"]]


def register(registry):
    @registry.case("resolver_integration_none")
    def resolver_integration_none(context):
        original = context.api("/api/config")["config"]
        assert original["dns"]["resolver_integration"] == "dnsmasq", original["dns"]
        resolver_stages = {"reload_dnsmasq", "verify_dnsmasq", "reload_fallback"}

        # dnsmasq -> none: the old integration is deactivated, the resolver
        # stages of this very apply are skipped and the resolver health fields
        # are reported as disabled.
        config = context.api("/api/config")["config"]
        config["dns"] = {"resolver_integration": "none"}
        health = context.apply_config(config)
        operation = health["lifecycle_operation"]
        for stage in operation["stages"]:
            if stage["id"] in ("reload_dnsmasq", "verify_dnsmasq"):
                assert stage["status"] == "skipped", operation
        assert health["resolver_integration"] == "none", health
        assert health["resolver_config_probe_status"] == "disabled", health
        assert not health.get("resolver_config_hash"), health
        assert not health.get("resolver_config_hash_actual"), health
        assert health.get("resolver_config_sync_state") is None, health

        # With the integration off lifecycle operations have no resolver stages.
        stopped = context.api("/api/service/stop", "POST")
        context.wait_for(
            "stopped runtime",
            lambda: ((current := context.api("/api/health/service"))["status"] == "stopped" and
                     current.get("lifecycle_operation", {}).get("id") == stopped["operation_id"] and
                     current["lifecycle_operation"].get("status") == "succeeded"))
        stop_operation = context.api("/api/health/service")["lifecycle_operation"]
        assert stage_ids(stop_operation) == ["stop_routing"], stop_operation
        started = context.api("/api/service/start", "POST")
        context.wait_for(
            "started runtime",
            lambda: ((current := context.api("/api/health/service"))["status"] == "running" and
                     current.get("lifecycle_operation", {}).get("id") == started["operation_id"] and
                     current["lifecycle_operation"].get("status") == "succeeded"))
        start_operation = context.api("/api/health/service")["lifecycle_operation"]
        assert stage_ids(start_operation) == ["start_routing"], start_operation
        assert not resolver_stages & set(stage_ids(start_operation)), start_operation
        health = context.health_running()
        assert health["resolver_config_probe_status"] == "disabled", health

        # none -> dnsmasq restores the managed resolver.
        context.apply_config(original)

        def converged():
            current = context.health_running()
            assert current["resolver_integration"] == "dnsmasq", current
            assert current.get("resolver_config_sync_state") == "converged", current
            return True

        context.wait_for("resolver convergence", converged)
