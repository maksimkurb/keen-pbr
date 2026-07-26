import re
import shlex


DISPATCHERS = ("KeenPbrTable", "KeenPbrTable_OUTPUT")
GENERATIONS = ("KeenPbrTable_A", "KeenPbrTable_B")


def _mangle(context) -> str:
    return context.run("iptables-save", "-t", "mangle").stdout


def _dispatcher_target(context) -> str:
    rules = context.run(
        "iptables", "-t", "mangle", "-S", "KeenPbrTable").stdout.splitlines()
    jumps = [line.rsplit(" ", 1)[-1] for line in rules
             if line.startswith("-A KeenPbrTable -j KeenPbrTable_")]
    assert len(jumps) == 1 and jumps[0] in GENERATIONS, rules
    return jumps[0]


def _assert_converged(context) -> None:
    state = _mangle(context)
    for chain in (*DISPATCHERS, *GENERATIONS):
        assert state.count(f":{chain} ") == 1, state
    assert state.count("-A PREROUTING -j KeenPbrTable\n") == 1, state
    assert state.count("-A OUTPUT -j KeenPbrTable_OUTPUT\n") == 1, state

    target = _dispatcher_target(context)
    assert state.count(f"-A KeenPbrTable -j {target}\n") == 1, state
    assert state.count(f"-A KeenPbrTable_OUTPUT -j {target}\n") == 1, state
    assert "kpbr-integration-garbage" not in state, state

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
        line = re.sub(r"KeenPbrTable_[AB]", "KeenPbrTable_SLOT", line)
        line = re.sub(r"kpbr([46])[sS]_", r"kpbr\1_SLOT_", line)
        lines.append(line)
    return tuple(sorted(lines))


def _apply(context, config) -> None:
    context.apply_config(config)
    _assert_converged(context)


def _delete_dispatchers(context) -> None:
    context.run("iptables", "-t", "mangle", "-D", "PREROUTING", "-j",
                "KeenPbrTable", check=False)
    context.run("iptables", "-t", "mangle", "-D", "OUTPUT", "-j",
                "KeenPbrTable_OUTPUT", check=False)
    for chain in DISPATCHERS:
        context.run("iptables", "-t", "mangle", "-F", chain, check=False)
        context.run("iptables", "-t", "mangle", "-X", chain, check=False)


def _delete_all_owned_chains(context) -> None:
    _delete_dispatchers(context)
    for chain in GENERATIONS:
        context.run("iptables", "-t", "mangle", "-F", chain, check=False)
        context.run("iptables", "-t", "mangle", "-X", chain, check=False)


def register(registry):
    @registry.case("iptables_ab_convergence", backends=("iptables",))
    def iptables_ab_convergence(context):
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

        _delete_dispatchers(context)
        _apply(context, config)

        target = _dispatcher_target(context)
        inactive = GENERATIONS[1] if target == GENERATIONS[0] else GENERATIONS[0]
        context.run("iptables", "-t", "mangle", "-F", inactive)
        context.run("iptables", "-t", "mangle", "-X", inactive)
        _apply(context, config)

        target = _dispatcher_target(context)
        for dispatcher in DISPATCHERS:
            context.run("iptables", "-t", "mangle", "-F", dispatcher)
        context.run("iptables", "-t", "mangle", "-F", target)
        context.run("iptables", "-t", "mangle", "-X", target)
        _apply(context, config)

        _delete_all_owned_chains(context)
        _apply(context, config)

        context.run("iptables", "-t", "mangle", "-D", "PREROUTING", "-j",
                    "KeenPbrTable")
        _apply(context, config)

        target = _dispatcher_target(context)
        inactive = GENERATIONS[1] if target == GENERATIONS[0] else GENERATIONS[0]
        context.run("iptables", "-t", "mangle", "-A", inactive, "-m", "comment",
                    "--comment", "kpbr-integration-garbage", "-j", "RETURN")
        _apply(context, config)

        context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-j",
                    "RETURN")
        _apply(context, config)

        target = _dispatcher_target(context)
        wrong = GENERATIONS[1] if target == GENERATIONS[0] else GENERATIONS[0]
        context.run("iptables", "-t", "mangle", "-F", "KeenPbrTable")
        context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-j",
                    target)
        context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-j",
                    wrong)
        _apply(context, config)

        context.run("iptables", "-t", "mangle", "-N", "KeenPbrTable_Unknown")
        context.run("iptables", "-t", "mangle", "-F", "KeenPbrTable")
        context.run("iptables", "-t", "mangle", "-A", "KeenPbrTable", "-j",
                    "KeenPbrTable_Unknown")
        _apply(context, config)
        context.run("iptables", "-t", "mangle", "-F", "KeenPbrTable_Unknown")
        context.run("iptables", "-t", "mangle", "-X", "KeenPbrTable_Unknown")

        context.run("iptables", "-t", "mangle", "-A", "PREROUTING", "-j",
                    "KeenPbrTable")
        context.run("iptables", "-t", "mangle", "-A", "OUTPUT", "-j",
                    "KeenPbrTable_OUTPUT")
        _apply(context, config)

        active = _dispatcher_target(context)
        inactive = GENERATIONS[1] if active == GENERATIONS[0] else GENERATIONS[0]
        failed_restore = "\n".join((
            "*mangle",
            ":KeenPbrTable - [0:0]",
            ":KeenPbrTable_A - [0:0]",
            ":KeenPbrTable_B - [0:0]",
            f"-F {inactive}",
            "-F KeenPbrTable",
            f"-A {inactive} -m set --match-set kpbr4_missing_integration dst -j RETURN",
            f"-A KeenPbrTable -j {inactive}",
            "COMMIT",
            "",
        ))
        result = context.run(
            "sh", "-c",
            f"printf %s {shlex.quote(failed_restore)} | "
            "iptables-restore --noflush --counters",
            check=False)
        assert result.returncode != 0, result.stdout
        assert _dispatcher_target(context) == active
        _assert_converged(context)

        inactive_set = ("kpbr4S_routed" if active == GENERATIONS[0]
                        else "kpbr4s_routed")
        failed_ipset = "\n".join((
            f"flush {inactive_set}",
            f"add {inactive_set} 203.0.113.77 -exist",
            "add kpbr4_missing_integration 203.0.113.78 -exist",
            "",
        ))
        result = context.run(
            "sh", "-c",
            f"printf %s {shlex.quote(failed_ipset)} | ipset restore -exist",
            check=False)
        assert result.returncode != 0, result.stdout
        assert _dispatcher_target(context) == active
        _apply(context, config)
        context.run("ipset", "test", inactive_set, "198.51.100.1")
        result = context.run("ipset", "test", inactive_set, "203.0.113.77",
                             check=False)
        assert result.returncode != 0
