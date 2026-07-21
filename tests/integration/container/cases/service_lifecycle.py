def register(registry):
    @registry.case("service_lifecycle")
    def service_lifecycle(context):
        health = context.health_running()
        assert health["os_type"] == "debian", health
        routing = context.api("/api/health/routing")
        assert routing["overall"] == "ok", routing
        assert routing["firewall_backend"] == context.backend, routing
        stopped = context.api("/api/service/stop", "POST")
        assert stopped["status"] == "ok", stopped
        context.wait_for("stopped runtime",
                         lambda: context.api("/api/health/service")["status"] == "stopped")
        started = context.api("/api/service/start", "POST")
        assert started["status"] == "ok", started
        context.wait_for("restarted runtime", context.health_running)
