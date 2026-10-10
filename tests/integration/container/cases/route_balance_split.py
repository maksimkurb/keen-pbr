"""Statistical split of balance-mode classification.

N new UDP flows (distinct source ports, so distinct 5-tuples and one conntrack
entry plus one classification decision each) are sent from the client through
the router.  A counting sink in every WAN namespace is the independent
observation; on iptables the Prometheus classification counters are
cross-checked against it (both iptables flavours).  The tolerance is +-2 percentage points: at N=10000
the standard error of a binomial share is at most 0.5 pp, so 2 pp is ~4 sigma.
"""

import json
import re
import time
import urllib.request
import uuid

from .route_balance import assert_balance_health

CONTAINER = "/mnt/payload/tests/integration/container"
SINK_PORT = 19030
FLOWS = 10000
TOLERANCE_PP = 2.0
FIRST_PORT = 20000
DESTINATIONS = {"ipv4": ("198.18.0.10", "192.0.2.2"),
                "ipv6": ("2001:db8:100::10", "2001:db8:1::2")}
EXTRA_NS = "kpbr-wan3"
EXTRA_HOST_PREFIX = ("ip", "netns", "exec", EXTRA_NS)


class Wan:
    """One balance candidate: tag, weight and how to run a command in its namespace."""

    def __init__(self, tag, weight, runner):
        self.tag, self.weight, self.runner = tag, weight, runner


def balance_config(context, wans):
    config = context.api("/api/config")["config"]
    outbounds = [
        {"tag": "wan_direct", "type": "interface", "interface": "wan_direct",
         "gateway": "10.10.0.2", "gateway6": "2001:db8:10::2"},
        {"tag": "wan_pbr", "type": "interface", "interface": "wan_pbr",
         "gateway": "10.20.0.2", "gateway6": "2001:db8:20::2"},
    ]
    if any(wan.tag == "wan_third" for wan in wans):
        outbounds.append({"tag": "wan_third", "type": "interface",
                          "interface": "wan_third", "gateway": "10.30.0.2",
                          "gateway6": "2001:db8:30::2"})
    outbounds.append({
        "tag": "auto", "type": "urltest",
        "url": "http://198.18.0.10:18080/health", "interval_ms": 1000,
        "probe_timeout_ms": 500, "strategy": "balance",
        "conntrack_on_switch": "preserve",
        "retry": {"attempts": 1, "interval_ms": 100},
        "circuit_breaker": {"failure_threshold": 1, "success_threshold": 1,
                            "timeout_ms": 1000, "half_open_max_requests": 1},
        "outbound_groups": [{"members": [
            {"outbound": wan.tag, "weight": wan.weight} for wan in wans]}]})
    config["outbounds"] = outbounds
    config["lists"] = {}
    config["route"] = {"inbound_interfaces": ["lan0"], "rules": [
        {"outbound": "auto", "dest_addr": "198.18.0.10/32"},
        {"outbound": "auto", "dest_addr": "2001:db8:100::10/128"},
    ]}
    return config


def wait_all_active(context, tags):
    def status():
        state = context.api("/api/runtime/outbounds")
        automatic = next(item for item in state["outbounds"] if item["tag"] == "auto")
        candidates = {item["outbound_tag"]: item["status"]
                      for item in automatic.get("interfaces", [])}
        return state if (set(candidates) == set(tags) and
                         all(value == "active" for value in candidates.values())) else False

    return context.wait_for("all balance candidates active", status)


def metrics_counts():
    """{(candidate, family): connections} from /metrics (iptables only)."""
    with urllib.request.urlopen("http://127.0.0.1:12121/metrics", timeout=8) as response:
        text = response.read().decode()
    counts = {}
    for line in text.splitlines():
        if not line.startswith("keen_pbr_balance_classifications_total{"):
            continue
        labels = dict(re.findall(r'(\w+)="([^"]*)"', line))
        if labels.get("outbound") != "auto":
            continue
        key = (labels["candidate"], labels["family"])
        counts[key] = counts.get(key, 0) + int(float(line.rsplit(" ", 1)[1]))
    return counts


def mark_rule_packets(context, family, destination):
    """Packet counters of the balance MARK rules for `destination`, in rule
    (= member) order, from `iptables-save -c`: the kernel's own count."""
    tool = "iptables-save" if family == "ipv4" else "ip6tables-save"
    host = destination + ("/32" if family == "ipv4" else "/128")
    packets = []
    for line in context.run(tool, "-c", "-t", "mangle").stdout.splitlines():
        match = re.match(r"\[(\d+):\d+\] -A KeenPbrTable .*-d " + re.escape(host) +
                         r" .*-j MARK --set-xmark", line)
        if match:
            packets.append(int(match.group(1)))
    return packets


def counter_snapshot(context, wans, family, destination):
    packets = mark_rule_packets(context, family, destination)
    assert len(packets) == len(wans), ("balance MARK rules", packets)
    metrics = metrics_counts()
    return {"packets": packets, "metrics": metrics}


def check_kernel_counters(context, wans, family, destination, before, counted):
    after = counter_snapshot(context, wans, family, destination)
    delta = {wan.tag: now - then for wan, now, then in
             zip(wans, after["packets"], before["packets"])}
    assert delta == counted, ("iptables rule counters disagree with WAN-side count",
                              delta, counted)
    # Rules carry ownership comments on both iptables flavours (the xt_comment
    # probe is flavour-aware), so the Prometheus counters must exist and agree
    # with the WAN-side sinks.
    def metric_delta():
        now = metrics_counts()
        return {wan.tag: now.get((wan.tag, family), 0) -
                before["metrics"].get((wan.tag, family), 0) for wan in wans}

    measured = context.wait_for("classification counters", lambda: (
        metric_delta() if sum(metric_delta().values()) >= FLOWS else False), timeout=20)
    assert measured == counted, ("metrics disagree with WAN-side count", measured, counted)
    print("KPBR_BALANCE_SPLIT metrics_crosscheck=ok", flush=True)


def start_sink(wan, run_id):
    output = f"/tmp/kpbr-sink-{run_id}-{wan.tag}.json"
    wan.runner("sh", "-c",
               f"rm -f {output} {output}.ready; "
               f"setsid python3 {CONTAINER}/probe.py sink --port {SINK_PORT} "
               f"--output {output} </dev/null >/dev/null 2>&1 & echo $! > {output}.pid")
    return output


def collect_sink(context, wan, output):
    wan.runner("sh", "-c", f"kill -TERM $(cat {output}.pid)")

    def done():
        result = wan.runner("cat", output, check=False)
        return json.loads(result.stdout) if result.returncode == 0 and result.stdout else False

    return context.wait_for(f"{wan.tag} sink result", done, timeout=15)


def wait_sink_ready(context, wan, output):
    context.wait_for(f"{wan.tag} sink ready", lambda: wan.runner(
        "test", "-e", output + ".ready", check=False).returncode == 0, timeout=10)


def run_split(context, wans, family):
    """Send FLOWS flows of one family; returns {tag: share in percent}."""
    destination, source = DESTINATIONS[family]
    run_id = uuid.uuid4().hex[:8]
    sinks = {wan.tag: start_sink(wan, run_id) for wan in wans}
    for wan in wans:
        wait_sink_ready(context, wan, sinks[wan.tag])
    before = (counter_snapshot(context, wans, family, destination)
              if context.backend == "iptables" else None)
    started = time.monotonic()
    context.client("python3", f"{CONTAINER}/probe.py", "burst",
                   "--destination", destination, "--destination-port", str(SINK_PORT),
                   "--source", source, "--first-port", str(FIRST_PORT),
                   "--count", str(FLOWS), timeout=120)
    sent_seconds = time.monotonic() - started
    time.sleep(1)  # let the last datagrams drain through the sinks
    results = {wan.tag: collect_sink(context, wan, sinks[wan.tag]) for wan in wans}

    counted = {tag: result[family]["flows"] for tag, result in results.items()}
    datagrams = {tag: result[family]["datagrams"] for tag, result in results.items()}
    other = "ipv6" if family == "ipv4" else "ipv4"
    assert all(result[other]["datagrams"] == 0 for result in results.values()), results
    assert datagrams == counted, ("duplicate flows at sink", datagrams, counted)
    total = sum(counted.values())
    assert total == FLOWS, ("datagrams lost or leaked", FLOWS, counted)

    if context.backend == "iptables":
        check_kernel_counters(context, wans, family, destination, before, counted)

    shares = {tag: 100.0 * count / total for tag, count in counted.items()}
    flavour = (context.run("iptables", "-V").stdout.strip().replace(" ", "_")
               if context.backend == "iptables" else "-")
    print(f"KPBR_BALANCE_SPLIT backend={context.backend} iptables={flavour} family={family} "
          f"flows={FLOWS} send_s={sent_seconds:.1f} counts={counted} "
          f"shares={ {tag: round(share, 2) for tag, share in shares.items()} }", flush=True)
    return shares


def assert_split(wans, shares, family):
    total_weight = sum(wan.weight for wan in wans)
    for wan in wans:
        target = 100.0 * wan.weight / total_weight
        assert abs(shares[wan.tag] - target) <= TOLERANCE_PP, (
            f"{family} {wan.tag}: share {shares[wan.tag]:.2f}% is more than "
            f"{TOLERANCE_PP} pp from target {target:.2f}%", shares)


def setup_third_wan(context):
    """A third WAN namespace (the shared topology has two)."""
    teardown_third_wan(context)
    run = context.run
    run("ip", "netns", "add", EXTRA_NS)
    run("ip", "link", "add", "wan_third", "type", "veth", "peer", "name", "kpbr_third_peer")
    run("ip", "link", "set", "kpbr_third_peer", "netns", EXTRA_NS)
    run("ip", "-n", EXTRA_NS, "link", "set", "kpbr_third_peer", "name", "wan_third")
    run("ip", "-n", EXTRA_NS, "link", "set", "lo", "up")
    for sysctl in ("net.ipv6.conf.all.disable_ipv6=0", "net.ipv4.conf.all.rp_filter=0"):
        run(*EXTRA_HOST_PREFIX, "sysctl", "-q", "-w", sysctl)
    for sysctl in ("net.ipv6.conf.wan_third.disable_ipv6=0",
                   "net.ipv4.conf.wan_third.rp_filter=0"):
        run("sysctl", "-q", "-w", sysctl)
    run("ip", "address", "add", "10.30.0.1/24", "dev", "wan_third")
    run("ip", "-6", "address", "add", "2001:db8:30::1/64", "dev", "wan_third", "nodad")
    run("ip", "link", "set", "wan_third", "up")
    run("ip", "-n", EXTRA_NS, "link", "set", "wan_third", "up")
    run("ip", "-n", EXTRA_NS, "address", "add", "10.30.0.2/24", "dev", "wan_third")
    run("ip", "-n", EXTRA_NS, "-6", "address", "add", "2001:db8:30::2/64",
        "dev", "wan_third", "nodad")
    run("ip", "-n", EXTRA_NS, "address", "add", "198.18.0.10/32", "dev", "lo")
    run("ip", "-n", EXTRA_NS, "-6", "address", "add", "2001:db8:100::10/128",
        "dev", "lo", "nodad")
    run("ip", "-n", EXTRA_NS, "route", "replace", "192.0.2.0/24", "via", "10.30.0.1")
    run("ip", "-n", EXTRA_NS, "-6", "route", "replace", "2001:db8:1::/64",
        "via", "2001:db8:30::1")
    # Health endpoint for the urltest probe (the sink only listens on UDP).
    run("sh", "-c",
        f"setsid ip netns exec {EXTRA_NS} python3 {CONTAINER}/probe.py server "
        f"--identity wan_third --log /tmp/kpbr-wan3-observations.jsonl "
        f"--ports 18080 </dev/null >/dev/null 2>&1 & echo $! > /tmp/kpbr-wan3.pid")
    context.wait_for("third WAN health endpoint", lambda: run(
        *EXTRA_HOST_PREFIX, "python3", "-c",
        "import socket; socket.create_connection(('198.18.0.10', 18080), 1)",
        check=False).returncode == 0, timeout=10)


def teardown_third_wan(context):
    context.run("sh", "-c",
                "kill $(cat /tmp/kpbr-wan3.pid) 2>/dev/null; rm -f /tmp/kpbr-wan3.pid",
                check=False)
    context.run("ip", "netns", "del", EXTRA_NS, check=False)
    context.run("ip", "link", "del", "wan_third", check=False)


def split_case(context, weights):
    names = ["wan_direct", "wan_pbr", "wan_third"][:len(weights)]
    runners = {"wan_direct": lambda *a, **k: context.wan("direct", *a, **k),
               "wan_pbr": lambda *a, **k: context.wan("pbr", *a, **k),
               "wan_third": lambda *a, **k: context.run(*EXTRA_HOST_PREFIX, *a, **k)}
    wans = [Wan(tag, weight, runners[tag]) for tag, weight in zip(names, weights)]
    third = "wan_third" in names
    try:
        if third:
            setup_third_wan(context)
        context.apply_config(balance_config(context, wans))
        wait_all_active(context, names)
        assert_balance_health(context)
        for family in ("ipv4", "ipv6"):
            assert_split(wans, run_split(context, wans, family), family)
    finally:
        if third:
            teardown_third_wan(context)


def register(registry):
    @registry.case("route_balance_split_two", requires=("balance_numgen", "balance_statistic"))
    def route_balance_split_two(context):
        split_case(context, (30, 70))

    @registry.case("route_balance_split_three", requires=("balance_numgen", "balance_statistic"))
    def route_balance_split_three(context):
        split_case(context, (30, 50, 20))
