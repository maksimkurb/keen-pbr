"""End-to-end DNS hold, CNAME learning, reapply, and bypass checks."""

import json

from .dns_common import dns_config
from .firewall_corruption_recovery import reapply, wait_degraded, wait_healthy
from integration_context import TEST_IP6


def _config(context):
    config = dns_config(
        context,
        [{"tag": "upstream", "address": "10.20.0.2:15353"}],
        {"learned": {"domains": ["target.cname.test"]}},
    )
    config["route"] = {"inbound_interfaces": ["lan0"],
                        "rules": [{"list": ["learned"], "outbound": "wan_pbr"}]}
    config["intercept"] = {
        "enabled": True,
        "dns": {"enabled": True, "hold_timeout_ms": 100},
        "l7": {"enabled": False},
        "min_ttl_s": 30,
        "max_ttl_s": 3600,
    }
    return config


def _query(context, qtype, server="192.0.2.1", port=53):
    result = context.client(
        "python3", "/mnt/payload/tests/integration/container/dns-fixture.py",
        "query", "--server", server, "--port", str(port),
        "--name", "alias.cname.test", "--qtype", str(qtype),
        check=False, timeout=12)
    assert result.returncode == 0, result.stderr
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    assert lines, result.stdout
    return json.loads(lines[-1])


def _kill_without_cleanup(context):
    pid = context.run("systemctl", "show", "--value", "--property", "MainPID",
                      "keen-pbr.service").stdout.strip()
    assert pid.isdigit() and int(pid) > 1, pid
    # Keep the firewall hooks installed: this is the missing-listener path,
    # distinct from the graceful stop path that removes them first.
    context.run("kill", "-KILL", pid)
    def exited():
        if context.run("kill", "-0", pid, check=False).returncode == 0:
            return False
        return context.run("systemctl", "is-active", "keen-pbr.service",
                           check=False).stdout.strip() != "active"

    context.wait_for("keen-pbr listener exit", exited)


def register(registry):
    @registry.case("dns_interception", requires=("nfqueue",))
    def dns_interception(context):
        config = _config(context)
        context.apply_config(config)
        context.wait_intercept(dns_hold=True, l7=False)

        # The CNAME answer is intentionally matched through the CNAME target,
        # not the queried alias.  This is a real client receive through the
        # NFQUEUE path; set publication is checked after receive and is not
        # presented as packet-ordering proof.
        try:
            context.dynamic_set_contains(list_name="learned")
        except AssertionError:
            pass
        else:
            raise AssertionError("DNS dynamic set was populated before the query")
        result = _query(context, 1)
        assert result["qname"] == "alias.cname.test", result
        assert "06746172676574" in result["packet_hex"], result
        assert "c612000a" in result["packet_hex"], result
        context.wait_for("CNAME address publication",
                         lambda: context.dynamic_set_contains(list_name="learned"))

        result6 = _query(context, 28)
        assert result6["qtype"] == 28, result6
        assert "20010db8010000000000000000000010" in result6["packet_hex"], result6
        context.wait_for("CNAME IPv6 address publication", lambda: (
            context.dynamic_set_contains(TEST_IP6, list_name="learned")))

        # Bypass the router resolver entirely: this query traverses the client→router→WAN
        # path to the upstream fixture's real port 53.  Clearing the learned
        # address first prevents a pre-populated set from making this a false
        # positive.
        context.dynamic_set_delete(list_name="learned")
        context.wait_for("forwarded IPv4 set clear", lambda: not context.dynamic_set_present(
            list_name="learned"))
        forwarded = _query(context, 1, "10.20.0.2")
        assert "c612000a" in forwarded["packet_hex"], forwarded
        context.wait_for("forwarded IPv4 DNS learning",
                         lambda: context.dynamic_set_contains(list_name="learned"))

        context.dynamic_set_delete(TEST_IP6, list_name="learned")
        context.wait_for("forwarded IPv6 set clear", lambda: not context.dynamic_set_present(
            TEST_IP6, list_name="learned"))
        forwarded6 = _query(context, 28, "2001:db8:20::2")
        assert "20010db8010000000000000000000010" in forwarded6["packet_hex"], forwarded6
        context.wait_for("forwarded IPv6 DNS learning", lambda: (
            context.dynamic_set_contains(TEST_IP6, list_name="learned")))

        # Forwarded and router-local resolver paths both use the same held
        # response path; the latter catches regressions in local OUTPUT flow.
        context.resolve("alias.cname.test", "198.18.0.10")
        context.resolve("alias.cname.test", TEST_IP6, "AAAA")
        local = context.run("dig", "+short", "+time=2", "+tries=1", "A",
                            "alias.cname.test", "@192.0.2.1", "-p", "53")
        assert "198.18.0.10" in local.stdout.split(), local.stdout

        # Dynamic sets are retained across an unrelated config apply by
        # default.  Add a list entry so this exercises the real apply path.
        reapplied = json.loads(json.dumps(config))
        reapplied["lists"]["learned"]["domains"].append("another.cname.test")
        context.apply_config(reapplied)
        context.wait_for("set survives reapply",
                         lambda: context.dynamic_set_contains(list_name="learned"))

        # Corrupt only the hook priority by moving the existing jump behind a
        # harmless foreign rule.  This exercises verifier ordering, rather
        # than the separate missing-hook path.
        harmless = ("-p", "udp", "--sport", "9", "-j", "RETURN")
        if context.backend == "iptables":
            for binary in ("iptables", "ip6tables"):
                context.run(binary, "-t", "mangle", "-I", "POSTROUTING", "1", *harmless)
        else:
            context.run("nft", "insert", "rule", "inet", "KeenPbrTable", "dns_hold",
                        "udp", "sport", "9", "counter", "return")
        wait_degraded(context, "DNS hold hook")
        reapply(context)
        wait_healthy(context)
        context.wait_intercept(dns_hold=True, l7=False)
        def hold_hook_restored():
            text = context.firewall_text()
            if context.backend == "iptables":
                lines = [line for line in text.splitlines()
                         if line.startswith("-A POSTROUTING")]
                hook = next((i for i, line in enumerate(lines)
                             if "KeenPbrDnsHold" in line), -1)
                foreign = next((i for i, line in enumerate(lines)
                                if "--sport 9" in line and "-j RETURN" in line), -1)
                return hook >= 0 and foreign >= 0 and hook < foreign
            queue = text.find("queue")
            foreign = text.find("sport 9")
            return queue >= 0 and (foreign < 0 or queue < foreign)

        context.wait_for("reconciled DNS hold hook", hold_hook_restored)
        if context.backend == "iptables":
            for binary in ("iptables", "ip6tables"):
                context.run(binary, "-t", "mangle", "-D", "POSTROUTING", *harmless,
                            check=False)

        # A dead listener is explicitly a bypass case.  Do not use SIGSTOP:
        # a stopped listener still owns queued packets and has no kernel hold
        # deadline guarantee.
        firewall = context.firewall_text()
        assert ("--queue-bypass" in firewall if context.backend == "iptables"
                else "bypass" in firewall), firewall
        _kill_without_cleanup(context)
        context.resolve("alias.cname.test", "198.18.0.10")
