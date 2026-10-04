from __future__ import annotations

import re
import shlex
import time

from integration_context import TEST_IP

from .routing_common import apply, probe

# One address rule (inline daddr match) and one list rule (set match). The list
# domains are never resolved here: only the rule shape matters.
RULES = [{"outbound": "wan_pbr", "dest_addr": f"{TEST_IP}/32"},
         {"outbound": "wan_pbr", "list": ["routed"]}]
LISTS = {"routed": {"domains": ["routed.test"]}}
FOREIGN_SOURCE = "203.0.113.1"
FOREIGN_NFT_TABLE = "kpbr_it_foreign"
# After this interval the next request queues a refresh, but it must still
# return the previous same-generation report while that refresh is in flight
# (src/daemon/daemon_core.cpp kRoutingHealthCacheLifetime).
HEALTH_CACHE_SECONDS = 5.5


# ---- health ---------------------------------------------------------------

def firewall_problems(report):
    """Rule checks that are not ok. A pending/unavailable report carries no
    rule checks at all, so an empty result never counts as a real verdict."""
    return [f"{check.get('action')}/{check.get('set_name')}={check['status']}: "
            f"{check.get('detail', '')}"
            for check in report.get("firewall_rules", []) if check["status"] != "ok"]


def wait_degraded(context, name):
    def degraded():
        report = context.api("/api/health/routing")
        problems = firewall_problems(report)
        return (report, problems) if report.get("overall") != "ok" and problems else False
    report, problems = context.wait_for(f"{name}: health to report firewall drift", degraded)
    return problems


def wait_healthy(context):
    def healthy():
        report = context.api("/api/health/routing")
        return report if (report.get("overall") == "ok" and report.get("firewall_rules")
                          and not firewall_problems(report)) else False
    return context.wait_for("routing health ok with a real firewall verdict", healthy)


def fresh_healthy(context):
    """The first stale-cache request keeps returning a real health verdict."""
    time.sleep(HEALTH_CACHE_SECONDS)
    report = context.api("/api/health/routing")
    assert (report.get("overall") == "ok" and report.get("firewall_rules")
            and not firewall_problems(report)), report
    return report


# ---- recovery trigger -----------------------------------------------------

def reapply(context):
    """SIGUSR1 makes the daemon re-apply the firewall rules (RulesOnly).

    The "SIGUSR1: firewall refresh complete" line is logged even when the
    refresh swallowed an exception, so success is proven only by the
    "Runtime iproute and firewall refresh complete." line logged after the
    apply. A deferred refresh (config operation busy) is retried by the daemon.
    """
    success = "Runtime iproute and firewall refresh complete."
    failure = "Runtime iproute and firewall refresh failed"
    sent = time.time()
    context.run("systemctl", "kill", "-s", "SIGUSR1", "keen-pbr.service")

    def completed():
        output = context.run(
            "journalctl", "--no-pager", "-u", "keen-pbr.service", f"--since=@{sent:.3f}",
            check=False).stdout
        if failure in output:  # not AssertionError: wait_for would retry it
            raise RuntimeError(f"SIGUSR1 runtime refresh failed:\n{output}")
        return (success in output
                and "SIGUSR1: firewall refresh complete" in output)
    context.wait_for("SIGUSR1 runtime refresh success", completed, timeout=25)


# ---- iptables corruption --------------------------------------------------

CHAIN = "KeenPbrTable"


def ipt_rules(context, chain=CHAIN):
    lines = context.run("iptables", "-t", "mangle", "-S", chain).stdout.splitlines()
    return [line for line in lines if line.startswith(f"-A {chain} ")]


def ipt_mark_rules(context):
    """1-based positions of our MARK rules: [address rule, list rule]."""
    rules = ipt_rules(context)
    positions = [i + 1 for i, rule in enumerate(rules) if " -j MARK " in rule]
    assert len(positions) == 2, rules
    return positions, rules


def ipt_spec(rule):
    return shlex.split(rule)[2:]  # drop "-A <chain>"


def ipt_wrong_mark(context):
    (first, _), rules = ipt_mark_rules(context)
    spec = ipt_spec(rules[first - 1])
    spec[spec.index("--set-xmark") + 1] = "0x40000/0xff0000"
    context.run("iptables", "-t", "mangle", "-R", CHAIN, str(first), *spec)


def ipt_missing(context):
    (first, _), _ = ipt_mark_rules(context)
    context.run("iptables", "-t", "mangle", "-D", CHAIN, str(first))


def ipt_duplicate(context):
    (first, _), rules = ipt_mark_rules(context)
    context.run("iptables", "-t", "mangle", "-I", CHAIN, str(first),
                *ipt_spec(rules[first - 1]))


def ipt_order(context):
    (first, _), rules = ipt_mark_rules(context)
    # MARK, CONNMARK, RETURN -> CONNMARK, MARK, RETURN
    context.run("iptables", "-t", "mangle", "-D", CHAIN, str(first))
    context.run("iptables", "-t", "mangle", "-I", CHAIN, str(first + 1),
                *ipt_spec(rules[first - 1]))


def ipt_hook(context):
    context.run("iptables", "-t", "mangle", "-D", "PREROUTING", "-j", CHAIN)


def ipt_unexpected(context):
    context.run("iptables", "-t", "mangle", "-I", CHAIN, "1",
                "-s", FOREIGN_SOURCE, "-j", "ACCEPT")


# ---- nftables corruption --------------------------------------------------

NFT_TABLE = ("inet", "KeenPbrTable")
NFT_CHAIN = "prerouting"
ADDRESS_RULE = ("ip", "daddr", TEST_IP)


def nft_mark_handles(context):
    """Handles of our mark jumps in prerouting: [address rule, v4 list, v6 list]."""
    text = context.run("nft", "-a", "list", "chain", *NFT_TABLE, NFT_CHAIN).stdout
    handles = re.findall(
        r"^\s*ip6? daddr .* jump setmark_\w+ .*# handle (\d+)$", text, re.M)
    assert len(handles) == 3, text
    return handles


def nft_wrong_mark(context):
    handle = nft_mark_handles(context)[0]
    context.run("nft", "replace", "rule", *NFT_TABLE, NFT_CHAIN, "handle", handle,
                *ADDRESS_RULE, "jump", "setmark_00040000")


def nft_missing(context):
    handle = nft_mark_handles(context)[0]
    context.run("nft", "delete", "rule", *NFT_TABLE, NFT_CHAIN, "handle", handle)


def nft_duplicate(context):
    handle = nft_mark_handles(context)[0]
    context.run("nft", "add", "rule", *NFT_TABLE, NFT_CHAIN, "position", handle,
                *ADDRESS_RULE, "jump", "setmark_00030000")


def nft_order(context):
    first, second, _ = nft_mark_handles(context)
    context.run("nft", "delete", "rule", *NFT_TABLE, NFT_CHAIN, "handle", first)
    context.run("nft", "add", "rule", *NFT_TABLE, NFT_CHAIN, "position", second,
                *ADDRESS_RULE, "jump", "setmark_00030000")


def nft_hook(context):
    # The nft hook is a base chain attribute: remove the whole base chain.
    context.run("nft", "flush", "chain", *NFT_TABLE, NFT_CHAIN)
    context.run("nft", "delete", "chain", *NFT_TABLE, NFT_CHAIN)


def nft_unexpected(context):
    context.run("nft", "insert", "rule", *NFT_TABLE, NFT_CHAIN,
                "ip", "saddr", FOREIGN_SOURCE, "accept")


# (name, corrupt function per backend, text the verifier must report,
#  does the corruption stop classification)
CORRUPTIONS = (
    ("wrong_mark", {"iptables": ipt_wrong_mark, "nftables": nft_wrong_mark},
     "rule mismatch", False),
    ("missing_rule", {"iptables": ipt_missing, "nftables": nft_missing},
     "rule missing", True),
    ("duplicate_rule", {"iptables": ipt_duplicate, "nftables": nft_duplicate},
     "duplicate rule", False),
    ("order_swapped", {"iptables": ipt_order, "nftables": nft_order},
     "rule order differs", False),
    ("hook_removed", {"iptables": ipt_hook, "nftables": nft_hook},
     "missing", True),
    ("unexpected_rule", {"iptables": ipt_unexpected, "nftables": nft_unexpected},
     "unexpected rule", False),
)


# ---- foreign rules in system chains --------------------------------------

def install_foreign(context):
    if context.backend == "iptables":
        for chain in ("PREROUTING", "OUTPUT"):  # after our hook jump
            context.run("iptables", "-t", "mangle", "-A", chain,
                        "-s", FOREIGN_SOURCE, "-j", "ACCEPT")
        return
    context.run("nft", "add", "table", "inet", FOREIGN_NFT_TABLE)
    context.run("nft", "add", "chain", "inet", FOREIGN_NFT_TABLE, "prerouting",
                "{", "type", "filter", "hook", "prerouting", "priority", "mangle",
                ";", "policy", "accept", ";", "}")
    context.run("nft", "add", "rule", "inet", FOREIGN_NFT_TABLE, "prerouting",
                "ip", "saddr", FOREIGN_SOURCE, "accept")


def foreign_present(context):
    if context.backend == "iptables":
        return all(f"-A {chain} -s {FOREIGN_SOURCE}/32 -j ACCEPT" in context.run(
            "iptables", "-t", "mangle", "-S", chain).stdout
            for chain in ("PREROUTING", "OUTPUT"))
    result = context.run("nft", "list", "table", "inet", FOREIGN_NFT_TABLE, check=False)
    return FOREIGN_SOURCE in result.stdout


def remove_foreign(context):
    if context.backend == "iptables":
        for chain in ("PREROUTING", "OUTPUT"):
            context.run("iptables", "-t", "mangle", "-D", chain,
                        "-s", FOREIGN_SOURCE, "-j", "ACCEPT", check=False)
    else:
        context.run("nft", "delete", "table", "inet", FOREIGN_NFT_TABLE, check=False)


def register(registry):
    @registry.case("firewall_corruption_recovery")
    def firewall_corruption_recovery(context):
        apply(context, RULES, LISTS)
        probe(context, "wan_pbr", destination_port=19000)
        baseline = firewall_text_of(context)

        for index, (name, corrupt, reported, stops_classification) in enumerate(CORRUPTIONS):
            corrupt[context.backend](context)
            assert firewall_text_of(context) != baseline, f"{name}: corruption changed nothing"
            problems = wait_degraded(context, name)
            assert any(reported in problem for problem in problems), (name, problems)
            print(f"KPBR_IT_NOTE case=firewall_corruption_recovery corruption={name} "
                  f"detected={problems}", flush=True)
            if stops_classification:
                probe(context, "wan_direct", destination_port=19000)
            if index == 0:
                # The daemon only reports drift: it must still be degraded after
                # a fresh verification, until a re-apply is requested.
                time.sleep(HEALTH_CACHE_SECONDS)
                assert wait_degraded(context, f"{name} (no auto-recovery)")

            reapply(context)
            wait_healthy(context)
            assert firewall_text_of(context) == baseline, f"{name}: ruleset not restored"
            probe(context, "wan_pbr", destination_port=19000)

        # Foreign rules in system chains are not ours: no degradation, and a
        # re-apply must not remove them.
        try:
            install_foreign(context)
            assert foreign_present(context)
            fresh_healthy(context)
            reapply(context)
            fresh_healthy(context)
            assert foreign_present(context), "re-apply removed a foreign rule"
            assert firewall_text_of(context) == baseline
            probe(context, "wan_pbr", destination_port=19000)
        finally:
            remove_foreign(context)


def firewall_text_of(context):
    """Our own chains and hook jumps; counters, handles and the deliberate
    foreign system-chain rules are left out."""
    if context.backend == "iptables":
        text = "\n".join(
            context.run("iptables", "-t", "mangle", "-S", chain).stdout
            for chain in ("KeenPbrTable", "KeenPbrOutput", "PREROUTING", "OUTPUT"))
        return "\n".join(line for line in text.splitlines() if not (
            line.startswith(("-A PREROUTING ", "-A OUTPUT ")) and FOREIGN_SOURCE in line))
    text = context.run("nft", "list", "table", *NFT_TABLE).stdout
    return re.sub(r"counter packets \d+ bytes \d+", "counter", text)
