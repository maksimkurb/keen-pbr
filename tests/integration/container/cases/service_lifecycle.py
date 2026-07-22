def register(registry):
    @registry.case("service_lifecycle")
    def service_lifecycle(context):
        health = context.health_running()
        assert health["os_type"] == "debian", health
        routing = context.api("/api/health/routing")
        assert routing["overall"] == "ok", routing
        assert routing["firewall_backend"] == context.backend, routing
        stopped = context.api("/api/service/stop", "POST")
        assert stopped["status"] == "accepted" and stopped["operation_id"], stopped
        context.wait_for(
            "stopped runtime",
            lambda: ((health := context.api("/api/health/service"))["status"] == "stopped" and
                     health.get("lifecycle_operation", {}).get("id") == stopped["operation_id"] and
                     health["lifecycle_operation"].get("status") == "succeeded"))
        started = context.api("/api/service/start", "POST")
        assert started["status"] == "accepted" and started["operation_id"], started
        context.wait_for(
            "restarted runtime",
            lambda: ((health := context.api("/api/health/service"))["status"] == "running" and
                     health.get("lifecycle_operation", {}).get("id") == started["operation_id"] and
                     health["lifecycle_operation"].get("status") == "succeeded"))
