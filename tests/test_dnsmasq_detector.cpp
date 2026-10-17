#include "../src/daemon/dnsmasq_detector.hpp"

#include "proc_fixture.hpp"

#include <doctest/doctest.h>

#include <sstream>

namespace keen_pbr3 {

using test::ProcFixture;

namespace {

constexpr const char* kHook = "/fake/dnsmasq-hook.sh";

struct Rig {
    ProcFixture fx;
    std::string inspect_text;
    int exit_code{0};
    bool timed_out{false};
    std::vector<std::vector<std::string>> exec_calls;
    std::vector<std::string> txt_calls;
    std::vector<std::string> resp_calls;
    std::function<DnsTxtProbeResult(const DnsmasqEndpoint&)> txt;
    std::function<bool(const DnsmasqEndpoint&)> responding;
    std::vector<InterfaceAddress> ifaces{{"lo", 4, "127.0.0.1"}, {"br-lan", 4, "192.168.1.1"}};

    Rig() {
        txt = [](const DnsmasqEndpoint&) {
            DnsTxtProbeResult r;
            r.status = DnsTxtProbeStatus::Missing;
            r.answered = true;
            return r;
        };
        responding = [](const DnsmasqEndpoint&) { return false; };
    }

    DnsmasqDetector make(const std::string& hook = kHook) {
        DnsmasqDetectorDeps d;
        d.proc_root = fx.root();
        d.exec = [this](const std::vector<std::string>& a) {
            exec_calls.push_back(a);
            ExecCaptureResult r;
            r.stdout_output = inspect_text;
            r.exit_code = exit_code;
            r.timed_out = timed_out;
            return r;
        };
        d.probe_txt = [this](const DnsmasqEndpoint& e, std::chrono::milliseconds) {
            txt_calls.push_back(e.str());
            return txt(e);
        };
        d.probe_responding = [this](const DnsmasqEndpoint& e, std::chrono::milliseconds) {
            resp_calls.push_back(e.str());
            return responding(e);
        };
        d.interface_addresses = [this] { return ifaces; };
        return DnsmasqDetector(hook, d);
    }

    void expect_single_inspect_call() const {
        REQUIRE(exec_calls.size() == 1);
        CHECK(exec_calls[0] == std::vector<std::string>{kHook, "inspect"});
    }
};

std::string record(const std::string& sel, const std::string& state, const std::string& extra = "",
                   const std::string& include = "managed") {
    std::ostringstream o;
    o << "[instance]\nselector=" << sel << "\nenabled=1\ndropin_include=" << include
      << "\ndropin_dir=/tmp/" << sel.substr(4) << ".d\ndropin_file=/tmp/" << sel.substr(4)
      << ".d/keen-pbr-upstream-dns.conf\ndropin_content=conf-script=x\ndropin_state=" << state << "\n"
      << extra;
    return o.str();
}

std::string header(const std::string& bin = "/usr/sbin/dnsmasq") {
    return "hook_version=2\nplatform=openwrt\nbinary=" + bin + "\n";
}

// Wildcard UDP+TCP socket on `port`, process `pid`.
void add_dns(ProcFixture& fx, int pid, const std::string& exe, const std::vector<std::string>& argv,
             const std::string& hexaddr, unsigned long inode, int ppid = 1) {
    fx.add_process(pid, exe, argv, ppid);
    fx.add_socket_fd(pid, 3, inode);
    fx.add_net_row(pid, "udp", hexaddr, "07", inode);
    fx.add_net_row(pid, "tcp", hexaddr, "0A", inode);
}

DnsTxtProbeResult stamp_result(const std::string& hash) {
    DnsTxtProbeResult r;
    r.status = DnsTxtProbeStatus::Ok;
    r.answered = true;
    r.txt = hash + "|1000|2000";
    return r;
}

std::vector<std::string> eps(const std::vector<DnsmasqEndpoint>& v) {
    std::vector<std::string> o;
    for (const auto& e : v) o.push_back(e.str());
    return o;
}

} // namespace

TEST_CASE("dnsmasq_detector: single instance on :53 responds") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/var/etc/a.conf\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/var/etc/a.conf"}, "00000000:0035", 11);
    auto det = rig.make().detect({});
    CHECK(det.hook_ok);
    CHECK(det.selection == DnsmasqSelectionKind::Automatic);
    REQUIRE(det.selected_instance());
    const auto& i = *det.selected_instance();
    REQUIRE(i.endpoint);
    CHECK(i.endpoint->str() == "127.0.0.1:53");
    CHECK(i.evidence == EndpointEvidence::Responding);
    CHECK(i.problem == DnsmasqErrorCode::None);
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: non-default port") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/var/etc/a.conf\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/var/etc/a.conf"}, "00000000:14E9", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    REQUIRE(det.selected_instance()->endpoint);
    CHECK(det.selected_instance()->endpoint->str() == "127.0.0.1:5353");
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: socket bound to one address only") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/c\n");
    // 192.168.1.1 little-endian hex 0101A8C0
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/c"}, "0101A8C0:0035", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(eps(det.selected_instance()->candidates) == std::vector<std::string>{"192.168.1.1:53"});
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: per-address listeners are ranked by address kind") {
    // bind-dynamic (OpenWrt default): one socket per address, kernel order.
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "current", "match_conf_file=/c\n");
    rig.fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/c"});
    const std::vector<std::string> rows{
        "057100CB:0035",  // 203.0.113.5, public
        "01004064:0035",  // 100.64.0.1, CGNAT
        "5CE2E30A:0035",  // 10.227.226.92
        "0100007F:0035",  // 127.0.0.1
        "0101A8C0:0035",  // 192.168.1.1
    };
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const unsigned long inode = 20 + i;
        rig.fx.add_socket_fd(100, static_cast<int>(3 + i), inode);
        rig.fx.add_net_row(100, "udp", rows[i], "07", inode);
    }
    rig.txt = [](const DnsmasqEndpoint&) { return stamp_result(std::string(32, 'a')); };

    DnsmasqDetectOptions o;
    o.expected_hash = std::string(32, 'a');
    auto det = rig.make().detect(o);
    REQUIRE(det.selected_instance());
    CHECK(eps(det.selected_instance()->candidates) ==
          std::vector<std::string>{"127.0.0.1:53", "10.227.226.92:53", "192.168.1.1:53",
                                   "100.64.0.1:53", "203.0.113.5:53"});
    REQUIRE(det.selected_instance()->endpoint);
    CHECK(det.selected_instance()->endpoint->str() == "127.0.0.1:53");

    // A previous endpoint keeps its place only inside its own tier.
    o.previous_endpoint = DnsmasqEndpoint{"192.168.1.1", 53};
    det = rig.make().detect(o);
    REQUIRE(det.selected_instance());
    CHECK(eps(det.selected_instance()->candidates) ==
          std::vector<std::string>{"127.0.0.1:53", "192.168.1.1:53", "10.227.226.92:53",
                                   "100.64.0.1:53", "203.0.113.5:53"});
    CHECK(det.selected_instance()->endpoint->str() == "127.0.0.1:53");
}

TEST_CASE("dnsmasq_detector: wildcard listener prefers LAN over WAN addresses") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/c\n");
    rig.ifaces = {{"wan", 4, "203.0.113.5"}, {"lo", 4, "127.0.0.1"}, {"br-lan", 4, "192.168.1.1"}};
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/c"}, "00000000:0035", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(eps(det.selected_instance()->candidates) ==
          std::vector<std::string>{"127.0.0.1:53", "192.168.1.1:53", "203.0.113.5:53"});
}

TEST_CASE("dnsmasq_detector: two instances, one installed is adopted") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "current", "match_conf_file=/a\n") +
                       record("uci:b", "absent", "match_conf_file=/b\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    add_dns(rig.fx, 101, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/b"}, "00000000:0036", 12);
    auto det = rig.make().detect({});
    CHECK(det.selection == DnsmasqSelectionKind::Adopted);
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->selector.str() == "uci:a");
    CHECK(det.warnings.empty());
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: two installed instances are ambiguous, no probes") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "current", "match_conf_file=/a\n") +
                       record("uci:b", "current", "match_conf_file=/b\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    add_dns(rig.fx, 101, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/b"}, "00000000:0036", 12);
    auto det = rig.make().detect({});
    CHECK(det.error == DnsmasqErrorCode::InstanceAmbiguous);
    CHECK_FALSE(det.selected.has_value());
    CHECK(rig.txt_calls.empty());
    CHECK(rig.resp_calls.empty());
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: none installed, ambiguous unless configured") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n") +
                       record("uci:b", "absent", "match_conf_file=/b\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    add_dns(rig.fx, 101, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/b"}, "00000000:0036", 12);
    {
        auto det = rig.make().detect({});
        CHECK(det.error == DnsmasqErrorCode::InstanceAmbiguous);
        CHECK_FALSE(det.selected.has_value());
    }
    {
        DnsmasqDetectOptions o;
        o.configured_selector = "uci:b";
        auto det = rig.make().detect(o);
        CHECK(det.selection == DnsmasqSelectionKind::Explicit);
        REQUIRE(det.selected_instance());
        CHECK(det.selected_instance()->selector.str() == "uci:b");
    }
    CHECK(rig.exec_calls.size() == 2);
}

TEST_CASE("dnsmasq_detector: configured selector not found") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent");
    DnsmasqDetectOptions o;
    o.configured_selector = "uci:nosuch";
    auto det = rig.make().detect(o);
    CHECK(det.error == DnsmasqErrorCode::InstanceNotFound);
    CHECK(det.message.find("uci:a") != std::string::npos);
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: selected instance not running") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n");
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->problem == DnsmasqErrorCode::NotRunning);
    CHECK(rig.txt_calls.empty());
    CHECK(rig.resp_calls.empty());
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: port=0 disables DNS") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "current", "match_conf_file=/a\n");
    rig.fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a", "--port=0"});
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->problem == DnsmasqErrorCode::DnsDisabled);
    CHECK(rig.txt_calls.empty());
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: hook failures") {
    {
        Rig rig;
        rig.exit_code = 2;
        auto det = rig.make().detect({});
        CHECK(det.error == DnsmasqErrorCode::NotInstalled);
        CHECK_FALSE(det.hook_ok);
        rig.expect_single_inspect_call();
    }
    {
        Rig rig;
        rig.exit_code = 1;
        rig.inspect_text = "boom";
        auto det = rig.make().detect({});
        CHECK(det.error == DnsmasqErrorCode::HookFailed);
        CHECK(det.message.find("boom") != std::string::npos);
        rig.expect_single_inspect_call();
    }
    {
        Rig rig;
        rig.timed_out = true;
        CHECK(rig.make().detect({}).error == DnsmasqErrorCode::HookFailed);
    }
    {
        Rig rig;
        auto det = rig.make("").detect({});
        CHECK(det.error == DnsmasqErrorCode::NoHook);
        CHECK(rig.exec_calls.empty());
    }
    {
        Rig rig;
        rig.inspect_text = "this is not valid\n";
        auto det = rig.make().detect({});
        CHECK(det.error == DnsmasqErrorCode::HookFailed);
        rig.expect_single_inspect_call();
    }
}

TEST_CASE("dnsmasq_detector: helper child is ignored, parent chosen") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n");
    add_dns(rig.fx, 200, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11, 1);
    rig.fx.add_process(150, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, 200);
    rig.fx.add_process(300, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, 200);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    REQUIRE(det.selected_instance()->process);
    CHECK(det.selected_instance()->process->pid == 200);
    CHECK(det.warnings.empty());
    CHECK(det.selected_instance()->warnings.empty());
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: static vs managed drop-in include") {
    for (const bool managed : {false, true}) {
        Rig rig;
        rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n",
                                             managed ? "managed" : "static");
        add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a", "-7", "/other"}, "00000000:0035", 11);
        auto det = rig.make().detect({});
        REQUIRE(det.selected_instance());
        CHECK(det.selected_instance()->problem ==
              (managed ? DnsmasqErrorCode::None : DnsmasqErrorCode::DropinNotLoaded));
        rig.expect_single_inspect_call();
    }
    // static and included -> None
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n", "static");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a", "-7", "/tmp/a.d,*.conf"}, "00000000:0035", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->dropin_dir_included);
    CHECK(det.selected_instance()->problem == DnsmasqErrorCode::None);
}

TEST_CASE("dnsmasq_detector: foreign drop-in") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "foreign", "match_conf_file=/a\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->problem == DnsmasqErrorCode::ForeignDropin);
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: socket owned but nothing answers") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    rig.txt = [](const DnsmasqEndpoint&) { return DnsTxtProbeResult{}; };
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    const auto& i = *det.selected_instance();
    CHECK(i.evidence == EndpointEvidence::SocketOwned);
    CHECK(i.problem == DnsmasqErrorCode::EndpointUnresponsive);
    CHECK(i.problem_detail.find("uci:a") != std::string::npos);
    CHECK(i.problem_detail.find("100") != std::string::npos);
    CHECK_FALSE(rig.resp_calls.empty());
    rig.expect_single_inspect_call();

    // Without probing, rule 7 must not fire.
    Rig rig2;
    rig2.inspect_text = rig.inspect_text;
    add_dns(rig2.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    DnsmasqDetectOptions o;
    o.probe = false;
    auto det2 = rig2.make().detect(o);
    REQUIRE(det2.selected_instance());
    CHECK(det2.selected_instance()->problem == DnsmasqErrorCode::None);
    CHECK_FALSE(det2.selected_instance()->probed);
    CHECK(rig2.txt_calls.empty());
}

TEST_CASE("dnsmasq_detector: stamp evidence") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "current", "match_conf_file=/a\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    rig.txt = [](const DnsmasqEndpoint&) { return stamp_result(std::string(32, 'a')); };
    {
        DnsmasqDetectOptions o;
        o.expected_hash = std::string(32, 'b');
        auto det = rig.make().detect(o);
        REQUIRE(det.selected_instance());
        CHECK(det.selected_instance()->evidence == EndpointEvidence::Responding);
        REQUIRE(det.selected_instance()->stamp);
        CHECK(det.selected_instance()->stamp->hash == std::string(32, 'a'));
    }
    {
        DnsmasqDetectOptions o;
        o.expected_hash = std::string(32, 'a');
        auto det = rig.make().detect(o);
        REQUIRE(det.selected_instance());
        CHECK(det.selected_instance()->evidence == EndpointEvidence::StampVerified);
    }
}

TEST_CASE("dnsmasq_detector: main_pid with renamed exe and static endpoint") {
    Rig rig;
    rig.inspect_text = header() + record("test:netns", "absent", "main_pid=77\nstatic_endpoint=192.0.2.1:53\n");
    rig.fx.add_process(77, "/tmp/kpbr-it-dnsmasq", {"kpbr-it-dnsmasq"});
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    const auto& i = *det.selected_instance();
    REQUIRE(i.process);
    CHECK(i.process->pid == 77);
    REQUIRE(i.endpoint);
    CHECK(i.endpoint->str() == "192.0.2.1:53");
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: exe_required mismatch") {
    Rig rig;
    rig.inspect_text = header("/opt/sbin/dnsmasq") +
                       record("entware:default", "absent", "exe_required=/opt/sbin/dnsmasq\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq"}, "00000000:0035", 11);
    auto det = rig.make().detect({});
    REQUIRE(det.selected_instance());
    CHECK(det.selected_instance()->problem == DnsmasqErrorCode::NotRunning);
    rig.expect_single_inspect_call();
}

TEST_CASE("dnsmasq_detector: single inspect call across scenarios") {
    Rig rig;
    rig.inspect_text = header() + record("uci:a", "absent", "match_conf_file=/a\n");
    add_dns(rig.fx, 100, "/usr/sbin/dnsmasq", {"dnsmasq", "-C", "/a"}, "00000000:0035", 11);
    DnsmasqDetectOptions o;
    o.probe_all = true;
    auto det = rig.make().detect(o);
    CHECK(det.instances.size() == 1);
    rig.expect_single_inspect_call();
}

} // namespace keen_pbr3
