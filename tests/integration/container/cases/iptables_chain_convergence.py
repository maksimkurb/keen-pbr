import re
import shlex


# Current layout (mangle table, non-raw mode): one classification chain per
# builtin hook, holding the rules directly.
OWNED = ("KeenPbrTable", "KeenPbrOutput")
HOOKS = (("PREROUTING", "KeenPbrTable"), ("OUTPUT", "KeenPbrOutput"))
# Retired A/B layout; apply must flush and delete these in the same commit.
LEGACY = ("KeenPbrTable_A", "KeenPbrTable_B", "KeenPbrTable_OUTPUT",
          "KeenPbrOutput_A", "KeenPbrOutput_B")


def _mangle(context) -> str:
    return context.run("iptables-save", "-t", "mangle").stdout


def _chain_declarations(state: str) -> list[str]:
    return [line[1:].split(" ", 1)[0]
            for line in state.splitlines() if line.startswith(":")]


def _assert_converged(context) -> None:
    state = _mangle(context)
    declarations = _chain_declarations(state)
    for chain in OWNED:
        assert declarations.count(chain) == 1, state
    for chain in LEGACY:
        assert chain not in declarations, state
    for builtin, chain in HOOKS:
        assert state.count(f"-A {builtin} -j {chain}\n") == 1, state
    assert "KeenPbrTable_" not in state, state
    assert "KeenPbrOutput_" not in state, state
    assert "203.0.113.254" not in state, state

    sets = {line.split()[1] for line in
            context.run("ipset", "save").stdout.splitlines()
            if line.startswith("create ")}
    referenced = set(re.findall(r"--match-set (\S+) dst", state))
    assert referenced <= sets, (sorted(referenced - sets), state)


def _logical_state(context) -> tuple[str, ...]:
    lines = []
    for line in (_mangle(context) + context.run("ipset", "save").stdout).splitlines():
        if "KeenPbr" not in line and "kpbr" not in line:
            continue
        line = re.sub(r"\[\d+:\d+\]", "[counter]", line)
        # Every refresh builds a new set with a fresh random hash seed.
        line = re.sub(r" initval 0x[0-9a-f]+", "", line)
        lines.append(line)
    return tuple(sorted(lines))


def _apply(context, config) -> None:
    context.apply_config(config)
    _assert_converged(context)


def _expect_apply_failure(context, config) -> None:
    staged = context.api("/api/config", "POST", config)
    assert staged["status"] == "ok", staged
    saved = context.api("/api/config/save", "POST")
    assert saved["status"] == "accepted" and saved["operation_id"], saved

    def failed_operation():
        health = context.api("/api/health/service")
        operation = health.get("lifecycle_operation", {})
        return (health if operation.get("id") == saved["operation_id"] and
                operation.get("status") == "failed" else False)

    context.wait_for("failed config lifecycle completion", failed_operation)


def _delete_hooks(context) -> None:
    for builtin, chain in HOOKS:
        context.run("iptables", "-t", "mangle", "-D", builtin, "-j", chain,
                    check=False)


def _delete_chains(context, chains) -> None:
    for chain in chains:
        context.run("iptables", "-t", "mangle", "-F", chain, check=False)
        context.run("iptables", "-t", "mangle", "-X", chain, check=False)


def _delete_all_owned_chains(context) -> None:
    _delete_hooks(context)
    context.run("iptables", "-t", "mangle", "-D", "OUTPUT", "-j",
                "KeenPbrTable_OUTPUT", check=False)
    _delete_chains(context, OWNED + LEGACY)


def _rules(context) -> str:
    """Rules of the owned chains without counters."""
    state = re.sub(r"\[\d+:\d+\]", "", _mangle(context))
    return "\n".join(l for l in state.splitlines() if not l.startswith("#"))


def _set_names(context) -> set[str]:
    return {line.split()[1] for line in
            context.run("ipset", "save").stdout.splitlines()
            if line.startswith("create ")}


def _make_legacy_ab_layout(context) -> None:
    """Rebuild the retired A/B layout by hand, as an old daemon left it."""
    for chain in ("KeenPbrTable_A", "KeenPbrTable_B", "KeenPbrTable_OUTPUT"):
        context.run("iptables", "-t", "mangle", "-N", chain)
    context.run("iptables", "-t", "mangle", "-D", "OUTPUT", "-j", "KeenPbrOutput")
    context.run("iptables", "-t", "mangle", "-F", "KeenPbrOutput")
    # Move the live rules into generation A, make KeenPbrTable a dispatcher.
    live = context.run("iptables", "-t", "mangle", "-S", "KeenPbrTable").stdout
    for line in live.splitlines():
        if line.startswith("-A KeenPbrTable "):
            context.run("sh", "-c", "iptables -t mangle " +
                        line.replace("-A KeenPbrTable ", "-A KeenPbrTable_A ", 1))
    context.run("iptables", "-t", "mangle", "-F", "KeenPbrTable")
    context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-j",
                "KeenPbrTable_A")
    context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable_OUTPUT", "-j",
                "KeenPbrTable_A")
    context.run("iptables", "-t", "mangle", "-A", "OUTPUT", "-j",
                "KeenPbrTable_OUTPUT")
    # A stale second generation holding junk.
    context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable_B", "-s",
                "203.0.113.254/32", "-j", "RETURN")


def register(registry):
    @registry.case("iptables_chain_convergence", backends=("iptables",))
    def iptables_chain_convergence(context):
        config = context.api("/api/config")["config"]
        config["lists"]["routed"]["ip_cidrs"] = ["198.51.100.0/24"]

        _apply(context, config)
        first = None
        for index in range(10):
            _apply(context, config)
            state = _logical_state(context)
            if index == 0:
                first = state
        assert _logical_state(context) == first

        # Missing hooks are restored, exactly one each.
        _delete_hooks(context)
        _apply(context, config)
        assert _logical_state(context) == first

        # Duplicated hooks are reduced to one.
        context.run("iptables", "-t", "mangle", "-A", "PREROUTING", "-j",
                    "KeenPbrTable")
        context.run("iptables", "-t", "mangle", "-A", "OUTPUT", "-j",
                    "KeenPbrOutput")
        _apply(context, config)

        # Chains and hooks gone entirely: recreated from scratch.
        _delete_all_owned_chains(context)
        _apply(context, config)
        assert _logical_state(context) == first

        # A single missing chain is recreated.
        _delete_hooks(context)
        _delete_chains(context, ("KeenPbrOutput",))
        _apply(context, config)
        assert _logical_state(context) == first

        # Foreign rules inside our chains are flushed by the next apply.
        context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-s",
                    "203.0.113.254/32", "-j", "RETURN")
        context.run("iptables", "-t", "mangle", "-A", "KeenPbrOutput", "-s",
                    "203.0.113.254/32", "-j", "RETURN")
        _apply(context, config)
        assert _logical_state(context) == first

        # Upgrade from the retired A/B layout: the legacy chains and the
        # legacy OUTPUT hook are removed in the same commit as the rewrite.
        _make_legacy_ab_layout(context)
        assert "KeenPbrTable_A" in _mangle(context)
        _apply(context, config)
        assert _logical_state(context) == first

        # Static sets keep their stable names; no generation sets exist.
        assert "kpbr4_routed" in _set_names(context)
        assert not [n for n in _set_names(context)
                    if re.match(r"kpbr[46][sSt]_", n)], _set_names(context)

        # Upgrade from the generation-named static sets: a leftover legacy
        # set is destroyed after the rules were restored.
        context.run("ipset", "create", "kpbr4S_routed", "hash:net", "-exist")
        context.run("ipset", "create", "kpbr6s_routed", "hash:net", "family",
                    "inet6", "-exist")
        _apply(context, config)
        assert not [n for n in _set_names(context)
                    if re.match(r"kpbr[46][sSt]_", n)], _set_names(context)
        assert _logical_state(context) == first

        # List content updates are visible through the same set name while
        # the rules stay byte-identical (refresh = temp set + swap).
        rules_before = _rules(context)
        result = context.run("ipset", "test", "kpbr4_routed", "203.0.113.5",
                             check=False)
        assert result.returncode != 0
        updated = context.api("/api/config")["config"]
        updated["lists"]["routed"]["ip_cidrs"] = ["198.51.100.0/24",
                                                  "203.0.113.0/24"]
        _apply(context, updated)
        context.run("ipset", "test", "kpbr4_routed", "203.0.113.5")
        context.run("ipset", "test", "kpbr4_routed", "198.51.100.1")
        assert _rules(context) == rules_before
        assert not [n for n in _set_names(context)
                    if re.match(r"kpbr[46][sSt]_", n)], _set_names(context)
        # ... and shrinking works the same way.
        _apply(context, config)
        result = context.run("ipset", "test", "kpbr4_routed", "203.0.113.5",
                             check=False)
        assert result.returncode != 0
        assert _rules(context) == rules_before

        # A failed restore leaves the previous state intact (one atomic
        # commit per table).
        failed_restore = "\n".join((
            "*mangle",
            ":KeenPbrTable - [0:0]",
            ":KeenPbrOutput - [0:0]",
            "-A KeenPbrTable -m set --match-set kpbr4_missing_integration dst -j RETURN",
            "-A KeenPbrOutput -j RETURN",
            "COMMIT",
            "",
        ))
        result = context.run(
            "sh", "-c",
            f"printf %s {shlex.quote(failed_restore)} | "
            "iptables-restore --noflush --counters",
            check=False)
        assert result.returncode != 0, result.stdout
        _assert_converged(context)
        assert _logical_state(context) == first

        # A failed ipset restore never touches the live set: the fill goes to
        # a temp set, so a broken script cannot empty or change the final one.
        failed_ipset = "\n".join((
            "create kpbr4t_routed hash:net family inet -exist",
            "add kpbr4t_routed 203.0.113.77 -exist",
            "add kpbr4_missing_integration 203.0.113.78 -exist",
            "",
        ))
        result = context.run(
            "sh", "-c",
            f"printf %s {shlex.quote(failed_ipset)} | ipset restore -exist",
            check=False)
        assert result.returncode != 0, result.stdout
        context.run("ipset", "test", "kpbr4_routed", "198.51.100.1")
        # The leftover temp set is dropped by the next apply.
        _apply(context, config)
        assert "kpbr4t_routed" not in _set_names(context)
        context.run("ipset", "test", "kpbr4_routed", "198.51.100.1")
        result = context.run("ipset", "test", "kpbr4_routed", "203.0.113.77",
                             check=False)
        assert result.returncode != 0
        assert _rules(context) == rules_before
