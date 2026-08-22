def register(registry):
    @registry.case("test_group_table")
    def test_group_table(context):
        table_id = "200"
        aggregate_mark = "65536/16711680"
        context.run("ip", "route", "replace", "table", table_id, "default",
                    "via", "10.20.0.2", "dev", "wan_pbr")
        try:
            config = context.api("/api/config")["config"]
            config["outbounds"] = [
                {"tag": "wan_direct", "type": "interface",
                 "interface": "wan_direct", "gateway": "10.10.0.2"},
                {"tag": "pbr_table", "type": "table", "table": int(table_id)},
                {"tag": "auto", "type": "urltest",
                 "url": "http://198.18.0.10:18080/health",
                 "interval_ms": 1000,
                 "probe_timeout_ms": 500,
                 "conntrack_on_switch": "preserve",
                 "retry": {"attempts": 1, "interval_ms": 100},
                 "outbound_groups": [
                     {"weight": 1, "outbounds": ["pbr_table"]},
                     {"weight": 2, "outbounds": ["wan_direct"]},
                 ]},
            ]
            config["route"] = {"inbound_interfaces": ["lan0"], "rules": [
                {"outbound": "auto", "dest_addr": "198.18.0.10/32"},
            ]}
            config["dns"]["rules"] = []
            context.apply_config(config)

            state = context.wait_for(
                "table candidate selection",
                lambda: context.selected_outbound("auto", "pbr_table"))
            automatic = next(item for item in state["outbounds"]
                             if item["tag"] == "auto")
            table_candidate = next(item for item in automatic["interfaces"]
                                   if item["outbound_tag"] == "pbr_table")
            assert table_candidate["status"] == "active", table_candidate
            assert isinstance(table_candidate.get("latency_ms"), int), table_candidate
            assert "interface_name" not in table_candidate, table_candidate
            context.assert_probe_path("wan_pbr")

            context.wait_for(
                "aggregate conntrack entry",
                lambda: context.run("conntrack", "-L", "--mark", aggregate_mark,
                                    check=False).stdout.strip())

            context.run("ip", "route", "del", "table", table_id, "default")
            context.wait_for("interface fallback selection",
                             lambda: context.selected_outbound("auto", "wan_direct"))
            context.wait_for(
                "failed table conntrack cleanup",
                lambda: not context.run("conntrack", "-L", "--mark", aggregate_mark,
                                        check=False).stdout.strip())
            context.assert_probe_path("wan_direct")
        finally:
            context.run("ip", "route", "flush", "table", table_id, check=False)
