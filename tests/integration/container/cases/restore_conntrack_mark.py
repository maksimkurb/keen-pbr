from __future__ import annotations

from integration_context import TEST_IP

from .routing_common import apply, probe, routing_config

# WAN fixtures listen on 18080,19000,19010,19011,19020 only. UDP probes from a
# fixed source port reuse one conntrack entry (same 5-tuple), which is the
# "established connection" whose saved mark must be restored.
PORT = 19010
PINNED_SOURCE_PORT = 41010
FRESH_SOURCE_PORT = 41011


def _udp(context, expected, source_port):
    payload = context.client_probe(proto="udp", destination=TEST_IP,
                                   destination_port=PORT,
                                   source_port=source_port)
    assert payload["identity"] == expected, payload


def register(registry):
    @registry.case("restore_conntrack_mark")
    def restore_conntrack_mark(context):
        try:
            apply(context, [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"}])
            context.run("conntrack", "-F", check=False)
            _udp(context, "wan_pbr", PINNED_SOURCE_PORT)

            # Re-route the destination without touching conntrack.
            context.apply_config(routing_config(
                context, [{"outbound": "wan_direct", "dest_addr": f"{TEST_IP}/32"}]))

            # The existing flow keeps its outbound through the restored
            # connection mark; a new flow follows the new rule.
            _udp(context, "wan_pbr", PINNED_SOURCE_PORT)
            _udp(context, "wan_direct", FRESH_SOURCE_PORT)

            # Negative control: without the conntrack entry the same 5-tuple
            # follows the new rule, so the step above depended on the restore.
            context.run("conntrack", "-F", check=False)
            _udp(context, "wan_direct", PINNED_SOURCE_PORT)

            # A plain TCP probe agrees with the new rule as well.
            probe(context, "wan_direct", destination_port=19000)
        finally:
            context.run("conntrack", "-F", check=False)
