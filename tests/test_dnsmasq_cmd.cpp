#include "../src/cmd/dnsmasq_cmd.hpp"

#include <doctest/doctest.h>

namespace keen_pbr3 {

namespace {

DnsmasqInstance make_instance(const std::string& selector, pid_t pid, const std::string& dir) {
    DnsmasqInstance in;
    in.hook.selector = selector;
    in.hook.dropin_dir = dir;
    in.hook.dropin_file = dir + "/keen-pbr.conf";
    ProcessInfo p;
    p.pid = pid;
    p.exe = "/usr/sbin/dnsmasq";
    in.process = p;
    SocketListener l;
    l.local = {"0.0.0.0", 53};
    l.udp = l.tcp = true;
    in.listeners.push_back(l);
    in.dropin_dir_included = true;
    return in;
}

DnsmasqDetection make_detection() {
    DnsmasqDetection d;
    d.hook_ok = true;
    d.inspect.platform = "openwrt";
    d.inspect.binary = "/usr/sbin/dnsmasq";

    auto a = make_instance("uci:cfg01411c", 1842, "/tmp/dnsmasq.cfg01411c.d");
    a.endpoint = DnsmasqEndpoint{"127.0.0.1", 53};
    a.evidence = EndpointEvidence::StampVerified;
    a.hook.dropin_state = DropinState::Current;
    a.stamp = DnsmasqConfigStamp{"3f2a9c0e1b7d4a55c6e8f0a1b2c3d4e5", 1, 2};

    auto b = make_instance("uci:guest", 2011, "/tmp/dnsmasq.guest.d");
    b.endpoint = DnsmasqEndpoint{"192.168.50.1", 53};
    b.evidence = EndpointEvidence::Responding;
    b.listeners[0].local = {"192.168.50.1", 53};

    d.instances = {a, b};
    d.selected = 0;
    d.selection = DnsmasqSelectionKind::Adopted;
    return d;
}

} // namespace

TEST_CASE("dnsmasq_cmd: text layout for selected and unselected instance") {
    const auto text = format_dnsmasq_detection_text(make_detection(), "/h.sh", "/c.json");
    const std::string expected =
        "dnsmasq hook: /h.sh (platform openwrt, binary /usr/sbin/dnsmasq)\n"
        "\n"
        "  uci:cfg01411c  [selected: adopted]\n"
        "    running:     yes (pid 1842)\n"
        "    enabled:     yes\n"
        "    endpoint:    127.0.0.1:53 (stamp_verified)\n"
        "    listeners:   udp+tcp 0.0.0.0:53\n"
        "    keen-pbr:    installed, loaded hash 3f2a9c0e1b7d4a55c6e8f0a1b2c3d4e5\n"
        "    conf-dir:    /tmp/dnsmasq.cfg01411c.d (included)\n"
        "    problem:     none\n"
        "\n"
        "  uci:guest\n"
        "    running:     yes (pid 2011)\n"
        "    enabled:     yes\n"
        "    endpoint:    192.168.50.1:53 (responding)\n"
        "    listeners:   udp+tcp 192.168.50.1:53\n"
        "    keen-pbr:    not installed\n"
        "    conf-dir:    /tmp/dnsmasq.guest.d (included)\n"
        "    problem:     none\n"
        "\n"
        "Selection: uci:cfg01411c (adopted)\n";
    CHECK(text == expected);
    CHECK(dnsmasq_detection_exit_code(make_detection()) == 0);
}

TEST_CASE("dnsmasq_cmd: unknown values, foreign drop-in, problem and warnings") {
    auto d = make_detection();
    d.instances[1].process.reset();
    d.instances[1].endpoint.reset();
    d.instances[1].listeners.clear();
    d.instances[1].hook.dropin_state = DropinState::Foreign;
    d.instances[1].dropin_dir_included = false;
    d.instances[1].problem = DnsmasqErrorCode::NotRunning;
    d.instances[1].problem_detail = "no process";
    d.warnings = {"something odd"};
    const auto text = format_dnsmasq_detection_text(d, "/h.sh", "/c.json");
    CHECK(text.find("    running:     no\n") != std::string::npos);
    CHECK(text.find("    endpoint:    -\n") != std::string::npos);
    CHECK(text.find("    listeners:   -\n") != std::string::npos);
    CHECK(text.find("foreign file at /tmp/dnsmasq.guest.d/keen-pbr.conf") != std::string::npos);
    CHECK(text.find("(not included)") != std::string::npos);
    CHECK(text.find("    problem:     DNSMASQ_NOT_RUNNING: no process\n") != std::string::npos);
    CHECK(text.find("warning: something odd\n") != std::string::npos);
}

TEST_CASE("dnsmasq_cmd: detection error tail lines") {
    DnsmasqDetection d;
    d.error = DnsmasqErrorCode::InstanceAmbiguous;
    d.message = "several instances";
    const auto text = format_dnsmasq_detection_text(d, "/h.sh", "/etc/c.json");
    CHECK(text.find("dnsmasq hook: /h.sh (platform -, binary -)\n") == 0);
    CHECK(text.find("Selection: none \xE2\x80\x94 DNSMASQ_INSTANCE_AMBIGUOUS: several instances\n") !=
          std::string::npos);
    const std::string tail =
        "Set \"dns\": {\"dnsmasq_instance\": \"<selector>\"} in /etc/c.json "
        "(or in the web UI settings), then apply the configuration.\n";
    CHECK(text.size() > tail.size());
    CHECK(text.compare(text.size() - tail.size(), tail.size(), tail) == 0);
    CHECK(dnsmasq_detection_exit_code(d) == 1);
}

TEST_CASE("dnsmasq_cmd: non-selection errors do not suggest dnsmasq_instance") {
    DnsmasqDetection d;
    d.error = DnsmasqErrorCode::NoHook;
    d.message = "no dnsmasq hook for this platform";
    const auto text = format_dnsmasq_detection_text(d, "", "/etc/c.json");
    CHECK(text.find("DNSMASQ_NO_HOOK") != std::string::npos);
    CHECK(text.find("dnsmasq_instance") == std::string::npos);
}

TEST_CASE("dnsmasq_cmd: selected instance with a problem exits 1") {
    auto d = make_detection();
    d.instances[0].problem = DnsmasqErrorCode::DropinNotLoaded;
    CHECK(dnsmasq_detection_exit_code(d) == 1);
}

TEST_CASE("dnsmasq_cmd: JSON keys") {
    const auto j = format_dnsmasq_detection_json(make_detection());
    for (const char* k : {"hook_ok", "platform", "binary", "selected", "selection", "error_code",
                          "message", "warnings", "instances"}) {
        CHECK_MESSAGE(j.contains(k), k);
    }
    CHECK(j["selected"] == "uci:cfg01411c");
    CHECK(j["selection"] == "adopted");
    CHECK(j["instances"].size() == 2);
    const auto& i = j["instances"][0];
    for (const char* k : {"selector", "enabled", "pid", "exe", "endpoint", "endpoint_evidence",
                          "listeners", "dropin_state", "dropin_dir_included", "stamp_hash",
                          "problem", "problem_detail", "warnings"}) {
        CHECK_MESSAGE(i.contains(k), k);
    }
    CHECK(i["pid"] == 1842);
    CHECK(i["listeners"][0] == "udp+tcp 0.0.0.0:53");
    CHECK(i["endpoint_evidence"] == "stamp_verified");
    CHECK(i["dropin_state"] == "current");
    CHECK(i["stamp_hash"] == "3f2a9c0e1b7d4a55c6e8f0a1b2c3d4e5");

    DnsmasqDetection none;
    const auto jn = format_dnsmasq_detection_json(none);
    CHECK(jn["selected"].is_null());
}

TEST_CASE("dnsmasq_cmd: argument validation") {
    CHECK(run_dnsmasq_command({}, "/nonexistent.json", "/h.sh") == 64);
    CHECK(run_dnsmasq_command({"bogus"}, "/nonexistent.json", "/h.sh") == 64);
    CHECK(run_dnsmasq_command({"remove", "uci:a"}, "/nonexistent.json", "/h.sh") == 64);
    CHECK(run_dnsmasq_command({"detect", "--bogus"}, "/nonexistent.json", "/h.sh") == 64);
}

} // namespace keen_pbr3
