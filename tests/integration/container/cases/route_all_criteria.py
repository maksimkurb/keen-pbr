from .routing_common import apply, probe


def register(registry):
    @registry.case("route_all_criteria")
    def route_all_criteria(context):
        listed = ["198.18.0.10/32"] + [f"198.18.0.{value}/32" for value in range(12, 21)]
        apply(context, [{
            "list": ["all_targets"], "outbound": "wan_pbr", "proto": "tcp",
            "dscp": 46, "src_port": "20000", "dest_port": "19010",
            "src_addr": "192.0.2.2/32", "dest_addr": "198.18.0.0/28",
        }], {"all_targets": {"ip_cidrs": listed}})
        common = {"proto": "tcp", "source": "192.0.2.2", "source_port": 20000,
                  "destination_port": 19010, "dscp": 46}
        probe(context, "wan_pbr", destination="198.18.0.10", **common)
        probe(context, "wan_direct", destination="198.18.0.11", **common)  # list only
        probe(context, "wan_direct", destination="198.18.0.12", **{**common, "proto": "udp"})
        probe(context, "wan_direct", destination="198.18.0.13", **{**common, "dscp": 0})
        probe(context, "wan_direct", destination="198.18.0.14",
              **{**common, "source_port": 20001})
        probe(context, "wan_direct", destination="198.18.0.15",
              **{**common, "destination_port": 19011})
        probe(context, "wan_direct", destination="198.18.0.16",
              **{**common, "source": "192.0.2.3"})
        probe(context, "wan_direct", destination="198.18.0.20", **common)
