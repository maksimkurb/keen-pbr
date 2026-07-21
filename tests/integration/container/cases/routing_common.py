from __future__ import annotations

import uuid


def routing_config(context, rules, lists=None):
    config = context.api("/api/config")["config"]
    config["outbounds"] = [
        {"tag": "wan_direct", "type": "interface", "interface": "wan_direct",
         "gateway": "10.10.0.2", "gateway6": "2001:db8:10::2"},
        {"tag": "wan_pbr", "type": "interface", "interface": "wan_pbr",
         "gateway": "10.20.0.2", "gateway6": "2001:db8:20::2"},
    ]
    config["lists"] = lists or {}
    config["route"] = {"inbound_interfaces": ["lan0"], "rules": rules}
    config["dns"]["rules"] = []
    return config


def apply(context, rules, lists=None):
    context.apply_config(routing_config(context, rules, lists))


def probe(context, expected, **values):
    context.run("conntrack", "-F", check=False)
    values.setdefault("token", uuid.uuid4().hex)
    return context.assert_probe_path(expected, **values)
