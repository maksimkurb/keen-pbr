"""keen-pbr managed dnsmasq: per-list upstreams through the platform hook.

The integration binary is built with KEEN_PBR_DNSMASQ_HOOK pointing at
tests/integration/netns/dnsmasq-hook.sh, which installs a conf-script drop-in
for the harness dnsmasq, restarts it and logs every call.
"""
import os
import uuid

from .dns_common import (HOOK_DROPIN, HOOK_FAIL_FLAG, HOOK_NODROPIN_FLAG,
                         assert_query_seen,
                         dnsmasq_health, dnsmasq_rules_config, hook_calls,
                         set_resolver_upstreams, wait_dnsmasq_state,
                         wait_hook_calls)

SELECTED4 = {"tag": "selected4", "address": "10.20.0.2:15353", "detour": "wan_pbr"}
# On start with mode none the daemon calls `remove` once to clean up leftovers.
STARTUP = ["remove"]
SELECTED6 = {"tag": "selected6", "address": "[2001:db8:20::2]:15354", "detour": "wan_pbr"}


def _rules_case(context, server, default_upstream, kind, identity):
    listed = f"listed-{uuid.uuid4().hex}.fixture.test"
    unlisted = f"unlisted-{uuid.uuid4().hex}.fixture.test"
    # The default upstream is the direct WAN; the selected server (and so
    # `pbr`) is only reachable through the per-list server= line.
    set_resolver_upstreams(context, default_upstream)
    config = dnsmasq_rules_config(
        context, [server],
        [{"list": ["scoped"], "server": server["tag"], "allow_domain_rebinding": True}],
        {"scoped": {"domains": [listed]}})
    context.apply_config(config)
    wait_hook_calls(context, STARTUP + ["apply"])
    health = wait_dnsmasq_state(context, "ok")
    assert health["mode"] == "dnsmasq", health
    assert health["rules"] == 1 and health["domains"] >= 1, health
    assert os.path.exists(HOOK_DROPIN)
    # `ok` means dnsmasq itself confirmed it serves this config (TXT stamp).
    assert health["probe_status"] == "ok", health
    assert health["loaded_hash"] == health["config_hash"], health
    assert health["loaded_boottime_ms"] > 0 and health["loaded_ts"] > 0, health

    context.resolve(listed, "198.18.0.10", "A")
    context.resolve(listed, "2001:db8:100::10", "AAAA")
    assert_query_seen(context, "pbr", kind, listed, identity)
    assert not [item for item in context.observations("direct", kind)
                if item.get("qname") == listed]

    context.resolve(unlisted, "198.18.0.11", "A")  # direct fixture answer
    assert not [item for item in context.observations("pbr", kind)
                if item.get("qname") == unlisted], "unlisted name reached selected server"
    assert [item for item in context.observations("direct", kind)
            if item.get("qname") == unlisted], "unlisted name missed the default upstream"


def register(registry):
    @registry.case("dnsmasq_rules_ipv4")
    def dnsmasq_rules_ipv4(context):
        _rules_case(context, SELECTED4, "10.10.0.2#15353", "dns4", "pbr-v4")

    @registry.case("dnsmasq_rules_ipv6")
    def dnsmasq_rules_ipv6(context):
        _rules_case(context, SELECTED6, "2001:db8:10::2#15354", "dns6", "pbr-v6")

    @registry.case("dnsmasq_fallback")
    def dnsmasq_fallback(context):
        name = f"fallback-{uuid.uuid4().hex}.fixture.test"
        # No harness upstream at all: only dns.fallback can resolve.
        set_resolver_upstreams(context)
        config = dnsmasq_rules_config(
            context, [SELECTED4], [], {},
            fallback=["selected4"], resolver_integration="dnsmasq")
        context.apply_config(config)
        wait_hook_calls(context, STARTUP + ["apply"])
        wait_dnsmasq_state(context, "ok")
        context.resolve(name, "198.18.0.10", "A")
        assert_query_seen(context, "pbr", "dns4", name, "pbr-v4")

    @registry.case("dnsmasq_hook_failure")
    def dnsmasq_hook_failure(context):
        listed = f"hookfail-{uuid.uuid4().hex}.fixture.test"
        later = f"hookfail-later-{uuid.uuid4().hex}.fixture.test"
        set_resolver_upstreams(context, "10.10.0.2#15353")
        open(HOOK_FAIL_FLAG, "w", encoding="utf-8").close()
        config = dnsmasq_rules_config(
            context, [SELECTED4],
            [{"list": ["scoped"], "server": "selected4"}],
            {"scoped": {"domains": [listed]}})
        # A failing hook must not fail the config apply or stop the service.
        context.apply_config(config)
        wait_hook_calls(context, STARTUP + ["apply"])
        health = wait_dnsmasq_state(context, "error")
        assert "simulated hook failure" in health["last_error"], health
        context.health_running()
        context.routing_health_running()

        os.unlink(HOOK_FAIL_FLAG)
        config["lists"]["scoped"]["domains"].append(later)
        context.apply_config(config)
        health = wait_dnsmasq_state(context, "ok")
        assert hook_calls() == STARTUP + ["apply", "apply"], hook_calls()
        assert health["domains"] >= 2 and not health.get("last_error"), health
        context.resolve(later, "198.18.0.10", "A")
        assert_query_seen(context, "pbr", "dns4", later, "pbr-v4")

    @registry.case("dnsmasq_missing_dropin")
    def dnsmasq_missing_dropin(context):
        # The hook exits 0 and restarts dnsmasq but never installs the
        # conf-script drop-in: dnsmasq runs without our config, so the TXT
        # stamp is missing and the state must be `error`, not `ok`.
        listed = f"nodropin-{uuid.uuid4().hex}.fixture.test"
        set_resolver_upstreams(context, "10.10.0.2#15353")
        open(HOOK_NODROPIN_FLAG, "w", encoding="utf-8").close()
        config = dnsmasq_rules_config(
            context, [SELECTED4],
            [{"list": ["scoped"], "server": "selected4"}],
            {"scoped": {"domains": [listed]}})
        try:
            context.apply_config(config)
            wait_hook_calls(context, STARTUP + ["apply"])
            # The daemon polls the stamp for ~15 s before giving up.
            health = wait_dnsmasq_state(context, "error", timeout=40)
            # dnsmasq forwards the unknown name upstream, so depending on the
            # fixture the probe sees NOERROR/NXDOMAIN (missing) or a timeout.
            assert health["probe_status"] in ("missing", "query_failed"), health
            assert health["last_error"], health
            if health["probe_status"] == "missing":
                assert "drop-in" in health["last_error"], health
            context.health_running()
        finally:
            os.unlink(HOOK_NODROPIN_FLAG)

    @registry.case("dnsmasq_disable")
    def dnsmasq_disable(context):
        listed = f"disable-{uuid.uuid4().hex}.fixture.test"
        set_resolver_upstreams(context, "10.10.0.2#15353")
        config = dnsmasq_rules_config(
            context, [SELECTED4],
            [{"list": ["scoped"], "server": "selected4"}],
            {"scoped": {"domains": [listed]}})
        context.apply_config(config)
        wait_hook_calls(context, STARTUP + ["apply"])
        wait_dnsmasq_state(context, "ok")
        assert os.path.exists(HOOK_DROPIN)

        config = dnsmasq_rules_config(
            context, [SELECTED4], [], {"scoped": {"domains": [listed]}},
            resolver_integration="none")
        context.apply_config(config)
        wait_hook_calls(context, STARTUP + ["apply", "remove"])
        health = wait_dnsmasq_state(context, "disabled")
        assert health["mode"] == "none", health
        assert not os.path.exists(HOOK_DROPIN)
