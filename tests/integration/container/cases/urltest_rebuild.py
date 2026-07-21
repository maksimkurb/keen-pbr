def register(registry):
    @registry.case("urltest_rebuild")
    def urltest_rebuild(context):
        context.wait_for("initial urltest selection", context.selected_outbound)
        context.api("/api/service/stop", "POST")
        context.wait_for("urltest runtime stop",
                         lambda: context.api("/api/health/service")["status"] == "stopped")
        context.api("/api/service/start", "POST")
        context.wait_for("urltest selection after routing rebuild", context.selected_outbound)
        context.resolve("routed.test", "198.18.0.10")
        context.assert_probe_path("wan_pbr", destination="198.18.0.10")
