import re

from .routing_common import apply, probe


TABLE = "kpbr_it_prefilter_mark"
CHAIN = "prerouting"


def _install_mark_fixture(context):
    context.run("nft", "delete", "table", "inet", TABLE, check=False)
    context.run("nft", "add", "table", "inet", TABLE)
    context.run(
        "nft", "add", "chain", "inet", TABLE, CHAIN,
        # Run before iptables RAW (-300), so the backend sees the fixture mark
        # as pre-existing input rather than a mark created after classification.
        "{", "type", "filter", "hook", "prerouting", "priority", "-310",
        ";", "policy", "accept", ";", "}",
    )
    context.run(
        "nft", "add", "rule", "inet", TABLE, CHAIN,
        "ip", "saddr", "192.0.2.2", "ip", "daddr", "198.18.0.10",
        "tcp", "dport", "19000", "counter", "meta", "mark", "set", "0x1",
    )


def _fixture_state(context):
    state = context.run("nft", "-a", "list", "chain", "inet", TABLE, CHAIN).stdout
    assert re.search(r"meta mark set 0x0*1", state), state
    assert re.search(r"counter packets [1-9][0-9]*", state), state


def register(registry):
    @registry.case("prefilter_skip_marked")
    def prefilter_skip_marked(context):
        apply(context, [{"outbound": "wan_pbr", "dest_addr": "198.18.0.10/32"}])
        probe(context, "wan_pbr", destination_port=19000)

        try:
            _install_mark_fixture(context)
            probe(context, "wan_direct", destination_port=19000)
            _fixture_state(context)
            context.run("nft", "delete", "table", "inet", TABLE)
            probe(context, "wan_pbr", destination_port=19000)
        finally:
            context.run("nft", "delete", "table", "inet", TABLE, check=False)
            context.run("conntrack", "-F", check=False)
