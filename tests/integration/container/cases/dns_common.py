from __future__ import annotations

import os

RUNTIME = os.environ.get("KPBR_RUNTIME", "/run/keen-pbr-it")
UPSTREAM_CONF = f"{RUNTIME}/dnsmasq-upstream.conf"


def dns_config(context, servers, lists=None):
    """Baseline config with the given dns.servers (used for DNS detours)."""
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "wan_direct", "type": "interface", "interface": "wan_direct",
         "gateway": "10.10.0.2", "gateway6": "2001:db8:10::2"},
        {"tag": "wan_pbr", "type": "interface", "interface": "wan_pbr",
         "gateway": "10.20.0.2", "gateway6": "2001:db8:20::2"},
    ]
    config["lists"] = lists or {}
    config["route"] = {"inbound_interfaces": ["lan0"], "rules": []}
    config["dns"] = {"servers": servers}
    return config


def set_resolver_upstreams(context, *upstreams):
    """Point the unmanaged test resolver (plain dnsmasq) at the given upstreams.

    keen-pbr does not configure the resolver; the harness resolver only
    forwards, so each case chooses where its queries go.  Upstreams use the
    dnsmasq `address#port` notation.
    """
    with open(UPSTREAM_CONF, "w", encoding="utf-8") as handle:
        for upstream in upstreams:
            handle.write(f"server={upstream}\n")
    context.run("systemctl", "restart", "dnsmasq.service")


def assert_query_seen(context, side, kind, name, identity):
    matches = [item for item in context.observations(side, kind)
               if item.get("qname") == name and item.get("identity") == identity]
    assert matches, (side, kind, name, context.observations(side, kind))


HOOK_LOG = f"{RUNTIME}/dnsmasq-hook.log"
HOOK_FAIL_FLAG = f"{RUNTIME}/dnsmasq-hook.fail"
HOOK_DROPIN = f"{RUNTIME}/dnsmasq.d/keen-pbr-upstream-dns.conf"


def hook_calls():
    """Actions the test KEEN_PBR_DNSMASQ_HOOK was invoked with, in order."""
    try:
        with open(HOOK_LOG, encoding="utf-8") as handle:
            return [line.strip() for line in handle if line.strip()]
    except FileNotFoundError:
        return []


def wait_hook_calls(context, expected, timeout=10):
    """Wait until the hook call log equals `expected` (async hook execution)."""
    return context.wait_for(f"dnsmasq hook calls {expected} (got {hook_calls()})",
                            lambda: hook_calls() == expected or False,
                            timeout=timeout)


def dnsmasq_health(context):
    return context.api("/api/health/service")["dnsmasq"]


def wait_dnsmasq_state(context, state, timeout=10):
    def reached():
        health = dnsmasq_health(context)
        return health if health["state"] == state else False

    return context.wait_for(f"dnsmasq health state {state}", reached, timeout=timeout)


def dnsmasq_rules_config(context, servers, rules, lists, **dns_extra):
    config = dns_config(context, servers, lists)
    config["dns"].update({"rules": rules, **dns_extra})
    return config
