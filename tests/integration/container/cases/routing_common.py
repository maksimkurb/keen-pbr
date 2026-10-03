from __future__ import annotations

import uuid

from integration_context import REMOTE_CONTAINER_DIR, TEST_IP, parse_probe


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
    return config


def apply(context, rules, lists=None):
    context.apply_config(routing_config(context, rules, lists))


def probe(context, expected, **values):
    context.run("conntrack", "-F", check=False)
    values.setdefault("token", uuid.uuid4().hex)
    return context.assert_probe_path(expected, **values)


# The pbr WAN host only routes back to the LAN. A router-originated packet
# re-routed into wan_pbr keeps the wan_direct source chosen by the main table.
RETURN_ROUTE = ("10.10.0.0/24", "via", "10.20.0.1", "dev", "wan_pbr")


def router_probe(context, expected, destination_port, mark=None):
    """Probe from the router itself (OUTPUT path, no input interface).

    `mark` sets SO_MARK on the probe socket, like a VPN client marking its own
    encrypted packets."""
    token = uuid.uuid4().hex
    context.wan("pbr", "ip", "route", "replace", *RETURN_ROUTE)
    try:
        context.run("conntrack", "-F", check=False)
        result = context.run(
            "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
            "--proto", "tcp", "--destination", TEST_IP,
            "--destination-port", str(destination_port), "--token", token,
            "--timeout", "4", *(("--mark", hex(mark)) if mark else ()),
            check=False, timeout=10)
        assert result.returncode == 0, result.stderr
        payload = parse_probe(result.stdout, token)
        assert payload["identity"] == expected, payload
    finally:
        context.wan("pbr", "ip", "route", "delete", *RETURN_ROUTE, check=False)
        context.run("conntrack", "-F", check=False)
