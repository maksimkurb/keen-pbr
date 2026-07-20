#!/usr/bin/env python3
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request

BACKEND = sys.argv[1]
API = "http://127.0.0.1:12121"
LAN = "kpbr-lan"
IP = "198.18.0.10"


def run(*args, check=True):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise AssertionError(f"command failed ({' '.join(args)}): {result.stderr.strip()}")
    return result


def api(path, method="GET", payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(API + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def wait_for(description, predicate, timeout=35):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = predicate()
            if last:
                return last
        except (AssertionError, OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
            last = exc
        time.sleep(0.25)
    raise AssertionError(f"timed out waiting for {description}: {last}")


def health_running():
    health = api("/api/health/service")
    assert health["status"] == "running", health
    assert health["runtime_state"] == "running", health
    return health


def resolve(domain):
    output = run("ip", "netns", "exec", LAN, "dig", "+short", domain, "@192.0.2.1").stdout
    assert IP in output.split(), output


def http_identity(domain):
    # DNS is asserted separately against the router resolver.  curl would
    # otherwise use the VM's unrelated /etc/resolv.conf, so pin the verified
    # answer while retaining the requested host and destination IP.
    output = run("ip", "netns", "exec", LAN, "curl", "--fail", "--silent", "--show-error",
                 "--noproxy", "*", "--resolve", f"{domain}:18080:{IP}",
                 "--max-time", "8", f"http://{domain}:18080/test").stdout
    payload = json.loads(output)
    assert payload["peer"] == "192.0.2.2", payload
    return payload["outbound"]


def command_output(*args):
    result = run(*args, check=False)
    return (f"$ {' '.join(args)} (exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}")


def routing_diagnostics():
    commands = [
        ("ip", "rule", "show"),
        ("ip", "route", "show", "table", "all"),
    ]
    if BACKEND == "iptables":
        commands.extend((("ipset", "list", "kpbr4d_routed"),
                         ("iptables-save", "-t", "mangle")))
    else:
        commands.append(("nft", "list", "table", "inet", "KeenPbrTable"))
    return "\n".join(command_output(*command) for command in commands)


def assert_http_outbound(domain, expected):
    actual = None

    def expected_outbound():
        nonlocal actual
        actual = http_identity(domain)
        assert actual == expected, f"{domain} was sent via {actual!r}, expected {expected!r}"
        return actual

    try:
        wait_for(f"{domain} data-plane route via {expected}", expected_outbound, timeout=10)
    except AssertionError as error:
        raise AssertionError(
            f"{error}\n--- policy-routing/firewall diagnostics ---\n{routing_diagnostics()}"
        ) from error


def dynamic_set_contains():
    if BACKEND == "iptables":
        run("ipset", "test", "kpbr4d_routed", IP)
    else:
        result = run("nft", "list", "set", "inet", "KeenPbrTable", "kpbr4d_routed")
        assert IP in result.stdout, result.stdout
    return True


def dnsmasq_pid():
    return run("systemctl", "show", "--value", "--property", "MainPID", "dnsmasq.service").stdout.strip()


def resolver_converged():
    health = health_running()
    assert health.get("resolver_config_sync_state") == "converged", health
    assert health.get("resolver_config_hash"), health
    assert health["resolver_config_hash"] == health.get("resolver_config_hash_actual"), health
    return health


def main():
    wait_for("keen-pbr service health", health_running)
    assert api("/api/health/service")["os_type"] == "debian"
    routing = api("/api/health/routing")
    assert routing["overall"] == "ok", routing
    assert routing["firewall_backend"] == BACKEND, routing
    assert run("systemctl", "is-active", "keen-pbr.service").stdout.strip() == "active"
    assert run("systemctl", "is-active", "dnsmasq.service").stdout.strip() == "active"

    def pbr_selected():
        state = api("/api/runtime/outbounds")
        auto = next((outbound for outbound in state["outbounds"] if outbound["tag"] == "auto"), None)
        assert auto is not None, state
        pbr = next((interface for interface in auto["interfaces"]
                    if interface["outbound_tag"] == "wan_pbr"), None)
        assert pbr is not None and pbr["status"] == "active", state
        return state
    wait_for("urltest selection of wan_pbr", pbr_selected)

    resolve("routed.test")
    wait_for("routed DNS address in dynamic backend set", dynamic_set_contains)
    assert_http_outbound("routed.test", "wan_pbr")

    resolve("direct.test")
    assert_http_outbound("direct.test", "wan_direct")

    before_dnsmasq_pid = dnsmasq_pid()
    config_response = api("/api/config")
    config = config_response["config"]
    config["lists"]["routed"]["domains"].append("added.test")
    staged = api("/api/config", "POST", config)
    assert staged["status"] == "ok", staged
    saved = api("/api/config/save", "POST")
    assert saved["status"] == "ok" and saved["saved"] and saved["applied"], saved

    def dnsmasq_restarted():
        current = dnsmasq_pid()
        assert current and current != before_dnsmasq_pid, (before_dnsmasq_pid, current)
        return current
    wait_for("dnsmasq restart after API config save", dnsmasq_restarted)
    wait_for("resolver config hash convergence", resolver_converged)
    resolve("added.test")
    wait_for("saved domain in dynamic backend set", dynamic_set_contains)
    assert_http_outbound("added.test", "wan_pbr")

    stopped = api("/api/service/stop", "POST")
    assert stopped["status"] == "ok", stopped
    wait_for("stopped runtime", lambda: api("/api/health/service")["status"] == "stopped")
    started = api("/api/service/start", "POST")
    assert started["status"] == "ok", started
    wait_for("restarted runtime", health_running)
    resolve("routed.test")
    assert_http_outbound("routed.test", "wan_pbr")


if __name__ == "__main__":
    main()
