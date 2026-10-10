"""Learning is scoped by the client's interface (L7 sniff, request direction)."""

import time

from .dns_common import dns_config, forget_cached_set_elements
from .l7_payloads import QUIC_INITIAL
from integration_context import REMOTE_CONTAINER_DIR, TEST_IP

# A LAN host the WAN side can reach through the router (the client netns has
# 192.0.2.3 assigned): a stand-in for a port-forwarded or IPv6 LAN address.
LAN_HOST = "192.0.2.3"
# Routed through the router toward wan_direct, owned by no namespace (every
# 198.18.0.10-20 is local to the WAN hosts, so a WAN-side client cannot use them).
FAR_HOST = "198.18.0.50"
# How long a sniffed packet is given to show up as a match before "not
# learned" is believed; NFLOG delivery takes milliseconds.
SETTLE = 3.0


def _config(context, inbound):
    config = dns_config(context,
        [{"tag": "upstream", "address": "10.20.0.2:15353"}], {
        "l7": {"domains": ["example.com"]},
    })
    config["route"] = {"inbound_interfaces": inbound,
                        "rules": [{"list": ["l7"], "outbound": "wan_pbr"}]}
    config["intercept"] = {
        "enabled": True,
        "dns": {"enabled": False},
        "l7": {"enabled": True, "tls": True, "http": True, "quic": True},
        "min_ttl_ms": 30000,
        "max_ttl_ms": 3600000,
    }
    return config


def _dns_config(context, inbound):
    config = _config(context, inbound)
    config["intercept"] = {"enabled": True, "dns": {"enabled": True},
                           "l7": {"enabled": False}}
    return config


def _hold_rules(context):
    """The DNS hold rules of the kernel dump."""
    marker = "-A KeenPbrDnsHold " if context.backend == "iptables" else "dns.intercept_hold"
    return [line for line in context.firewall_text().splitlines() if marker in line]


def _matched(context):
    return (context.intercept_health().get("counters") or {}).get("l7_matched", 0) or 0


def _quic_from(context, side, destination):
    """One QUIC Initial toward destination from the client (side None) or a WAN host."""
    args = ("python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
            "--proto", "udp", "--destination", destination,
            "--destination-port", "443", "--payload-hex", QUIC_INITIAL.hex(),
            "--token", "learning-scope", "--timeout", "4", "--no-receive")
    result = (context.client(*args, check=False, timeout=10) if side is None
              else context.wan(side, *args, check=False, timeout=10))
    assert result.returncode == 0, result.stderr


def _learned(context, destination):
    return context.dynamic_set_present(destination, list_name="l7")


def _forget(context, destination):
    context.dynamic_set_delete(destination, list_name="l7")
    context.wait_for(f"{destination} cleared",
                     lambda: not _learned(context, destination))
    forget_cached_set_elements(context)


def _assert_not_learned(context, side, destination, what):
    before = _matched(context)
    _quic_from(context, side, destination)
    time.sleep(SETTLE)
    assert _matched(context) == before, f"{what}: the packet was sniffed"
    assert not _learned(context, destination), f"{what}: the address was learned"


def _assert_learned(context, side, destination, what):
    before = _matched(context)
    _quic_from(context, side, destination)
    context.wait_for(f"{what}: dynamic address",
                     lambda: _learned(context, destination))
    context.wait_for(f"{what}: L7 match", lambda: _matched(context) > before)


def register(registry):
    @registry.case("learning_client_scope", requires=("nflog", "connbytes"))
    def learning_client_scope(context):
        # No allowlist: every interface but the outbound/WAN ones is a source
        # of learning.
        context.apply_config(_config(context, []))
        context.wait_intercept(dns_hold=False, l7=True)
        _forget(context, TEST_IP)
        _forget(context, LAN_HOST)

        _assert_learned(context, None, TEST_IP, "LAN client, empty inbound_interfaces")
        # A packet arriving from the internet toward a LAN address (port
        # forward, IPv6 LAN host) is not parsed, however well-formed it is.
        _assert_not_learned(context, "direct", LAN_HOST,
                            "WAN packet toward a LAN host")
        _assert_not_learned(context, "pbr", LAN_HOST,
                            "second WAN packet toward a LAN host")

        # An allowlist replaces the WAN denylist: only clients on the listed
        # interface are learned.  wan_pbr is listed here (the harness has no
        # second LAN segment), so its host stands in for the listed client and
        # the LAN client is the unlisted one.
        context.apply_config(_config(context, ["wan_pbr"]))
        context.wait_intercept(dns_hold=False, l7=True)
        _forget(context, FAR_HOST)
        # The pbr host has no route toward the far side except through us.
        far_route = ("198.18.0.0/24", "via", "10.20.0.1", "dev", "wan_pbr")
        context.wan("pbr", "ip", "route", "replace", *far_route)
        try:
            _assert_learned(context, "pbr", FAR_HOST, "client on a listed interface")
        finally:
            context.wan("pbr", "ip", "route", "delete", *far_route, check=False)
        _forget(context, FAR_HOST)
        _assert_not_learned(context, None, FAR_HOST, "client on an unlisted interface")
        # And the unlisted WAN interface is still not a source.
        _assert_not_learned(context, "direct", LAN_HOST,
                            "WAN packet toward a LAN host with an allowlist")


    @registry.case("learning_client_scope_dns_rules", requires=("nfqueue",))
    def learning_client_scope_dns_rules(context):
        # The held reply goes back to the client: the client interface is the
        # output interface of the DNS hold rules.
        context.apply_config(_dns_config(context, []))
        context.wait_intercept(dns_hold=True, l7=False)
        denied = "\n".join(_hold_rules(context))
        assert denied, context.firewall_text()
        for name in ("wan_direct", "wan_pbr"):
            assert name in denied, denied
        assert "lan0" not in denied, denied
        # Replies leaving through an outbound interface are never held.
        if context.backend == "iptables":
            assert "-o wan_direct" in denied and "-j RETURN" in denied, denied
        else:
            assert '"wan_direct", "wan_pbr" }' in denied and "oifname !=" in denied, denied

        context.apply_config(_dns_config(context, ["lan0"]))
        context.wait_intercept(dns_hold=True, l7=False)
        allowed = "\n".join(_hold_rules(context))
        assert "lan0" in allowed, allowed
        assert "wan_direct" not in allowed and "wan_pbr" not in allowed, allowed
        if context.backend == "iptables":
            assert "-o lan0 " in allowed and "RETURN" not in allowed, allowed
        else:
            assert 'oifname "lan0"' in allowed, allowed
