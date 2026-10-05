"""Actual TLS, HTTP, and QUIC payloads through the NFLOG path."""

from .dns_common import dns_config, forget_cached_set_elements
from .l7_payloads import QUIC_INITIAL, http_request, tls_client_hello
from integration_context import REMOTE_CONTAINER_DIR, TEST_IP, TEST_IP6


def _config(context):
    config = dns_config(context,
        [{"tag": "upstream", "address": "10.20.0.2:15353"}], {
        "l7": {"domains": ["example.com", "http.example.com"]},
    })
    config["route"] = {"inbound_interfaces": ["lan0"],
                        "rules": [{"list": ["l7"], "outbound": "wan_pbr"}]}
    config["intercept"] = {
        "enabled": True,
        "dns": {"enabled": False},
        "l7": {"enabled": True, "tls": True, "http": True, "quic": True},
        "min_ttl_s": 30,
        "max_ttl_s": 3600,
    }
    return config


def _counter(context, name):
    return (context.intercept_health().get("counters") or {}).get(name, 0) or 0


def _matching_observations(context, side, proto, payload):
    return [item for item in context.observations(side, "probe")
            if item.get("proto") == proto and item.get("payload_hex") == payload.hex()]


def register(registry):
    @registry.case("l7_interception", requires=("nflog", "connbytes"))
    def l7_interception(context):
        context.apply_config(_config(context))
        context.wait_intercept(dns_hold=False, l7=True)

        cases = (("tcp", 443, tls_client_hello("example.com"), "raw-tcp", TEST_IP),
                 ("tcp6", 443, tls_client_hello("example.com"), "raw-tcp", TEST_IP6),
                 ("tcp", 80, http_request("http.example.com"), "http", TEST_IP),
                 ("udp", 443, QUIC_INITIAL, "raw-udp", TEST_IP))
        for proto_name, port, payload, observation_proto, destination in cases:
            proto = "tcp" if proto_name == "tcp6" else proto_name
            context.dynamic_set_delete(destination, list_name="l7")
            context.wait_for(f"{proto}/{port} dynamic address clear",
                             lambda: not context.dynamic_set_present(destination,
                                                                       list_name="l7"))
            forget_cached_set_elements(context)  # the cache must not hide the deleted element
            direct_before = len(_matching_observations(context, "direct",
                                                        observation_proto, payload))
            pbr_before = len(_matching_observations(context, "pbr",
                                                     observation_proto, payload))
            matched_before = _counter(context, "l7_matched")
            deleted_before = _counter(context, "conntrack_deleted")

            # The route has no static CIDR for this list.  The first flow must
            # therefore use the topology's default direct route and only then
            # publish the address into the dynamic list.
            context.client_raw_probe(proto=proto, destination=destination, destination_port=port,
                                     payload_hex=payload.hex())
            context.wait_for(f"{proto}/{port} initial direct fixture receive",
                             lambda: len(_matching_observations(
                                 context, "direct", observation_proto, payload)) > direct_before)
            assert len(_matching_observations(context, "pbr", observation_proto,
                                               payload)) == pbr_before
            context.wait_for(f"{proto}/{port} dynamic address",
                             lambda: context.dynamic_set_contains(destination, list_name="l7"))
            context.wait_for(f"{proto}/{port} L7 match",
                             lambda: _counter(context, "l7_matched") > matched_before)
            context.wait_for(f"{proto}/{port} Added cleanup",
                             lambda: _counter(context, "conntrack_deleted") > deleted_before)

            # A fresh flow after publication must follow the learned dynamic
            # set to the PBR fixture, independently for each protocol.
            pbr_before = len(_matching_observations(context, "pbr",
                                                     observation_proto, payload))
            matched_before = _counter(context, "l7_matched")
            context.client_raw_probe(proto=proto, destination=destination, destination_port=port,
                                     payload_hex=payload.hex())
            context.wait_for(f"{proto}/{port} subsequent PBR fixture receive",
                             lambda: len(_matching_observations(
                                 context, "pbr", observation_proto, payload)) > pbr_before)
            context.wait_for(f"{proto}/{port} subsequent L7 match",
                             lambda: _counter(context, "l7_matched") > matched_before)

        # The Added result for every first flow requested asynchronous cleanup.
        context.wait_for("L7 conntrack cleanup request",
                         lambda: _counter(context, "conntrack_requests") > 0)
        context.wait_for("L7 conntrack cleanup completion",
                         lambda: _counter(context, "conntrack_deleted") > 0)

    @registry.case("conntrack_cleanup_client_scope", requires=("nflog", "connbytes"))
    def conntrack_cleanup_client_scope(context):
        """Learning triggered by client A resets only A's flows to the learned IP."""
        context.apply_config(_config(context))
        context.wait_intercept(dns_hold=False, l7=True)
        other_ip = "198.18.0.11"
        client_a, client_b = "192.0.2.2", "192.0.2.3"
        context.dynamic_set_delete(TEST_IP, list_name="l7")
        context.wait_for("dynamic address clear",
                         lambda: not context.dynamic_set_present(TEST_IP, list_name="l7"))
        forget_cached_set_elements(context)
        context.run("conntrack", "-F", check=False)

        def flow(source, destination, port, source_port, payload):
            result = context.client(
                "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client", "--proto", "udp",
                "--destination", destination, "--destination-port", str(port),
                "--source", source, "--source-port", str(source_port),
                "--payload-hex", payload.hex(), "--token", f"scope-{source_port}",
                "--no-receive", check=False, timeout=10)
            assert result.returncode == 0, result.stderr

        def tracked(source, destination, source_port):
            out = context.run("conntrack", "-L", "-p", "udp", check=False).stdout
            return any(f"src={source} dst={destination} sport={source_port} " in line
                       for line in out.splitlines())

        # Plain UDP (port 4444) is never parsed, so these flows only create
        # conntrack entries: A and B to the soon-learned IP, A to another IP.
        flow(client_a, TEST_IP, 4444, 41001, b"keep-or-reset")
        flow(client_b, TEST_IP, 4444, 41002, b"keep-or-reset")
        flow(client_a, other_ip, 4444, 41003, b"keep-or-reset")
        for source, destination, port in ((client_a, TEST_IP, 41001), (client_b, TEST_IP, 41002),
                                          (client_a, other_ip, 41003)):
            context.wait_for(f"{source}->{destination} tracked",
                             lambda: tracked(source, destination, port))

        deleted_before = _counter(context, "conntrack_deleted")
        flow(client_a, TEST_IP, 443, 41004, QUIC_INITIAL)  # client A triggers learning
        context.wait_for("dynamic address",
                         lambda: context.dynamic_set_contains(TEST_IP, list_name="l7"))
        context.wait_for("scoped cleanup",
                         lambda: _counter(context, "conntrack_deleted") > deleted_before)
        # Any client A flow to the learned IP is reset, even one on another port.
        assert not tracked(client_a, TEST_IP, 41001)
        # Client B's flow to the same IP and A's flow to another IP survive.
        assert tracked(client_b, TEST_IP, 41002)
        assert tracked(client_a, other_ip, 41003)
