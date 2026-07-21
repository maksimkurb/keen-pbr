import uuid

from .dns_common import assert_query_seen, dns_config


def register(registry):
    @registry.case("dns_upstream_ipv6")
    def dns_upstream_ipv6(context):
        name = f"ipv6-{uuid.uuid4().hex}.fixture.test"
        config = dns_config(context, [
            {"tag": "selected6", "address": "[2001:db8:20::2]:15354", "detour": "wan_pbr"},
        ], ["selected6"],
            [{"list": ["query"], "server": "selected6", "allow_domain_rebinding": True}],
            {"query": {"domains": [name]}})
        context.apply_config(config)
        context.resolve(name, "198.18.0.10", "A")
        context.resolve(name, "2001:db8:100::10", "AAAA")
        assert_query_seen(context, "pbr", "dns6", name, "pbr-v6")
        assert not [item for item in context.observations("direct", "dns6")
                    if item.get("qname") == name]
