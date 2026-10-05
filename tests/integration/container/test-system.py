#!/usr/bin/env python3
"""Registration, selection, execution, and reporting for system cases."""

from __future__ import annotations

import os
import sys

from case_engine import Registry, Runner, aggregate_status, write_summary
from integration_context import (SystemContext, diagnostics, preserve_diagnostic,
                                 setup_case, teardown_case)
from cases import (dns_no_leak, dns_routing_save, dns_upstream_ipv4,
                   dns_interception, l7_interception,
                   dns_upstream_ipv6, multiport_validation, route_all_criteria,
                   route_balance, route_dest_addr, route_dest_port, route_dscp, route_list,
                   route_drop, route_pass, route_proto, route_src_addr, route_src_port,
                   rule_shapes,
                   service_lifecycle, sigusr1_no_packet_leak, table_interface,
                   test_group_table, urltest_rebuild, iptables_chain_convergence,
                   prefilter_skip_marked, inbound_interface_filter, restore_conntrack_mark,
                   skip_established_or_dnat, local_reply_skip, forwarded_reply_skip, output_lan_skip,
                   firewall_corruption_recovery, dnsmasq_mgmt,
                   loop_safety_marked_socket, router_traffic_default_off)

CASE_MODULES = (
    service_lifecycle,
    dns_routing_save,
    dns_interception,
    l7_interception,
    urltest_rebuild,
    route_balance,
    iptables_chain_convergence,
    prefilter_skip_marked,
    inbound_interface_filter,
    restore_conntrack_mark,
    skip_established_or_dnat,
    local_reply_skip,
    forwarded_reply_skip,
    output_lan_skip,
    firewall_corruption_recovery,
    loop_safety_marked_socket,
    router_traffic_default_off,
    rule_shapes,
    route_pass,
    route_drop,
    table_interface,
    test_group_table,
    multiport_validation,
    route_list,
    route_proto,
    route_dscp,
    route_src_port,
    route_dest_port,
    route_src_addr,
    route_dest_addr,
    route_all_criteria,
    dns_upstream_ipv4,
    dns_upstream_ipv6,
    sigusr1_no_packet_leak,
    dns_no_leak,
    dnsmasq_mgmt,
)


def build_registry() -> Registry:
    registry = Registry()
    for module in CASE_MODULES:
        module.register(registry)
    return registry


def optional_missing(backend: str) -> set[str]:
    """Optional capabilities the preflight found absent for this backend."""
    path = os.path.join(os.environ.get("KPBR_RUNTIME", "/run/keen-pbr-it"),
                        "optional-missing")
    try:
        with open(path, encoding="utf-8") as handle:
            entries = [line.split() for line in handle]
    except FileNotFoundError:
        return set()
    return {entry[1] for entry in entries if len(entry) == 2 and entry[0] == backend}


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("iptables", "nftables"):
        print("usage: test-system.py <iptables|nftables>", file=sys.stderr)
        return 2
    backend = sys.argv[1]
    registry = build_registry()
    try:
        cases = registry.select(os.environ.get("INTEGRATION_CASES", "all"), backend)
    except ValueError as error:
        print(f"KPBR_IT_END backend={backend} status=invalid_cases message={str(error).replace(' ', '_')}")
        return 2
    context = SystemContext(backend)
    missing_optional = optional_missing(backend)
    timeout_seconds = float(os.environ.get("INTEGRATION_CASE_TIMEOUT", "180"))
    results = Runner(backend, cases, context, setup_case, teardown_case, diagnostics,
                     timeout_seconds=timeout_seconds,
                     preserve=preserve_diagnostic,
                     missing_optional=missing_optional).run()
    summary_path = os.environ.get("KPBR_IT_SUMMARY", "/mnt/seed/summary.json")
    write_summary(summary_path, backend, results)
    return aggregate_status(results)


if __name__ == "__main__":
    raise SystemExit(main())
