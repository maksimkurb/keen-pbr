import uuid

from .dns_common import assert_query_seen, dns_config


def register(registry):
    @registry.case("dns_upstream_ipv4")
    def dns_upstream_ipv4(context):
        name = f"ipv4-{uuid.uuid4().hex}.fixture.test"
        config = dns_config(context, [
            {"tag": "selected4", "address": "10.20.0.2:15353", "detour": "wan_pbr"},
        ], ["selected4"],
            [{"list": ["query"], "server": "selected4", "allow_domain_rebinding": True}],
            {"query": {"domains": [name]}})
        context.apply_config(config)
        context.resolve(name, "198.18.0.10", "A")
        context.resolve(name, "2001:db8:100::10", "AAAA")
        assert_query_seen(context, "pbr", "dns4", name, "pbr-v4")
        assert not [item for item in context.observations("direct", "dns4")
                    if item.get("qname") == name]
