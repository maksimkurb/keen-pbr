from __future__ import annotations


def dns_config(context, servers, fallback, rules=None, lists=None):
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "wan_direct", "type": "interface", "interface": "wan_direct",
         "gateway": "10.10.0.2", "gateway6": "2001:db8:10::2"},
        {"tag": "wan_pbr", "type": "interface", "interface": "wan_pbr",
         "gateway": "10.20.0.2", "gateway6": "2001:db8:20::2"},
    ]
    config["lists"] = lists or {}
    config["route"] = {"inbound_interfaces": ["lan0"], "rules": []}
    config["dns"] = {
        "system_resolver": {"address": "192.0.2.1"},
        "servers": servers,
        "fallback": fallback,
        "rules": rules or [],
    }
    return config


def assert_query_seen(context, side, kind, name, identity):
    matches = [item for item in context.observations(side, kind)
               if item.get("qname") == name and item.get("identity") == identity]
    assert matches, (side, kind, name, context.observations(side, kind))
