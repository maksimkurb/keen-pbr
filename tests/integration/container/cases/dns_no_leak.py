import uuid

from .dns_common import assert_query_seen, dns_config


def register(registry):
    @registry.case("dns_no_leak")
    def dns_no_leak(context):
        ok4 = f"ok4-{uuid.uuid4().hex}.leak.test"
        fail4 = f"fail4-{uuid.uuid4().hex}.leak.test"
        ok6 = f"ok6-{uuid.uuid4().hex}.leak.test"
        fail6 = f"fail6-{uuid.uuid4().hex}.leak.test"
        lists = {
            "detour4": {"domains": [ok4, fail4]},
            "detour6": {"domains": [ok6, fail6]},
        }
        servers = [
            {"tag": "selected4", "address": "10.20.0.2:15353", "detour": "wan_pbr"},
            {"tag": "selected6", "address": "[2001:db8:20::2]:15354", "detour": "wan_pbr"},
            {"tag": "fallback4", "address": "10.10.0.2:15353"},
            {"tag": "fallback6", "address": "[2001:db8:10::2]:15354"},
        ]
        rules = [
            {"list": ["detour4"], "server": "selected4", "allow_domain_rebinding": True},
            {"list": ["detour6"], "server": "selected6", "allow_domain_rebinding": True},
        ]
        context.apply_config(dns_config(context, servers, ["fallback4", "fallback6"],
                                        rules, lists))
        context.resolve(ok4, "198.18.0.10", "A")
        context.resolve(ok6, "2001:db8:100::10", "AAAA")
        assert_query_seen(context, "pbr", "dns4", ok4, "pbr-v4")
        assert_query_seen(context, "pbr", "dns6", ok6, "pbr-v6")
        assert not [item for kind in ("dns4", "dns6")
                    for item in context.observations("direct", kind)
                    if item.get("qname") in (ok4, ok6)]

        # Prove both decoys are reachable over the ordinary direct path before
        # using their empty logs as leak evidence.
        direct4 = f"direct4-{uuid.uuid4().hex}.fixture.test"
        direct6 = f"direct6-{uuid.uuid4().hex}.fixture.test"
        result4 = context.client("dig", "+short", "+time=2", "+tries=1", "A", direct4,
                                 "@10.10.0.2", "-p", "15353")
        result6 = context.client("dig", "+short", "+time=2", "+tries=1", "AAAA", direct6,
                                 "@2001:db8:10::2", "-p", "15354")
        assert "198.18.0.11" in result4.stdout.split(), result4.stdout
        assert "2001:db8:100::11" in result6.stdout.split(), result6.stdout
        assert_query_seen(context, "direct", "dns4", direct4, "direct-v4")
        assert_query_seen(context, "direct", "dns6", direct6, "direct-v6")
        for side in ("direct", "pbr"):
            context.wan(side, "sh", "-c",
                        f": > /run/kpbr-wan/{side}/dns-v4.jsonl; "
                        f": > /run/kpbr-wan/{side}/dns-v6.jsonl")

        context.wan("pbr", "sh", "-c",
                    "iptables -I INPUT -p udp --dport 15353 -j DROP; "
                    "iptables -I INPUT -p tcp --dport 15353 -j DROP; "
                    "ip6tables -I INPUT -p udp --dport 15354 -j DROP; "
                    "ip6tables -I INPUT -p tcp --dport 15354 -j DROP")
        context.resolve(fail4, (), "A", expect_success=False)
        context.resolve(fail6, (), "AAAA", expect_success=False)
        for side in ("direct", "pbr"):
            leaked = [item for kind in ("dns4", "dns6")
                      for item in context.observations(side, kind)
                      if item.get("qname") in (fail4, fail6)]
            assert not leaked, (side, leaked)
