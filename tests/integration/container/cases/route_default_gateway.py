from .routing_common import apply, probe


V4_ROUTE = ("198.18.0.0/24", "via", "10.10.0.2", "dev", "wan_direct")
V6_ROUTE = ("2001:db8:100::/64", "via", "2001:db8:10::2", "dev", "wan_direct")
V4_DEFAULT = ("default", "via", "10.10.0.2", "dev", "wan_direct")
V6_DEFAULT = ("default", "via", "2001:db8:10::2", "dev", "wan_direct")


def _bypass_entries(context, family):
    """Entries of the iptables default_gateway bypass set."""
    name = "kpbr4g_bypass" if family == 4 else "kpbr6g_bypass"
    result = context.run("ipset", "save", name, check=False)
    assert result.returncode == 0, result.stderr
    return {line.split()[2] for line in result.stdout.splitlines()
            if line.startswith("add ")}


def register(registry):
    @registry.case("route_dest_addr_negated_list")
    def route_dest_addr_negated_list(context):
        # Not to any of the listed addresses: on iptables the list used to
        # split into one negated rule per address, which matched everything.
        apply(context, [
            {"outbound": "wan_pbr", "dest_addr": "!198.18.0.10/32,198.18.0.11/32"},
        ])
        probe(context, "wan_direct", destination="198.18.0.10")
        probe(context, "wan_direct", destination="198.18.0.11")
        probe(context, "wan_pbr", destination="198.18.0.12")

    @registry.case("route_default_gateway")
    def route_default_gateway(context):
        # The test destinations have routes of their own in main, and such
        # routes are left alone: reach them through the default route instead.
        context.run("ip", "route", "delete", *V4_ROUTE)
        context.run("ip", "-6", "route", "delete", *V6_ROUTE)
        context.run("ip", "route", "replace", *V4_DEFAULT)
        context.run("ip", "-6", "route", "replace", *V6_DEFAULT)
        try:
            apply(context, [
                {"outbound": "wan_pbr", "default_gateway": "ipv4"},
                {"outbound": "wan_pbr", "default_gateway": "ipv6"},
            ])
            probe(context, "wan_pbr", destination="198.18.0.12")
            probe(context, "wan_pbr", destination="2001:db8:100::12")
            if context.backend == "iptables":
                # The networks of the router itself stay on the main table.
                entries = _bypass_entries(context, 4)
                assert "192.0.2.0/24" in entries, entries
                assert "10.20.0.0/24" in entries, entries
                assert "198.18.0.0/24" not in entries, entries
        finally:
            context.run("ip", "route", "delete", *V4_DEFAULT, check=False)
            context.run("ip", "-6", "route", "delete", *V6_DEFAULT, check=False)
            context.run("ip", "route", "replace", *V4_ROUTE, check=False)
            context.run("ip", "-6", "route", "replace", *V6_ROUTE, check=False)
