// Real dnsmasq tests (SPEC §17.2). Run only when KEEN_PBR_TEST_DNSMASQ names an
// executable dnsmasq; otherwise every case logs a message and returns.
#include "../src/daemon/dnsmasq_capability.hpp"
#include "../src/daemon/dnsmasq_detector.hpp"
#include "../src/dns/dns_client.hpp"
#include "../src/dns/dns_txt_probe.hpp"
#include "../src/dns/dnsmasq_gen.hpp"

#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace keen_pbr3 {

namespace {

constexpr const char* kStampHash = "0123456789abcdef0123456789abcdef";

std::optional<std::string> real_dnsmasq() {
    const char* env = std::getenv("KEEN_PBR_TEST_DNSMASQ");
    if (env == nullptr || *env == '\0' || ::access(env, X_OK) != 0) return std::nullopt;
    return std::string(env);
}

std::string current_user() {
    if (const passwd* pw = ::getpwuid(::geteuid())) return pw->pw_name;
    return "root";
}

std::optional<std::uint16_t> free_udp_port() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return std::nullopt;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    std::optional<std::uint16_t> port;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0) {
        port = ntohs(a.sin_port);
    }
    ::close(fd);
    return port;
}

// RAII dnsmasq child: killed and reaped on destruction.
class DnsmasqChild {
public:
    DnsmasqChild(const std::string& bin, std::uint16_t port) : port_(port) {
        const std::vector<std::string> args = {
            bin, "--keep-in-foreground", "--no-resolv", "--no-hosts", "--conf-file=/dev/null",
            "--listen-address=127.0.0.1", "--bind-interfaces", "--port=" + std::to_string(port),
            "--user=" + current_user(),
            std::string("--txt-record=") + std::string(kDnsmasqStampDomain) + "," + kStampHash + "|1|1",
            "--pid-file="};
        pid_ = ::fork();
        if (pid_ == 0) {
            std::vector<char*> argv;
            for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            ::execv(bin.c_str(), argv.data());
            ::_exit(127);
        }
    }
    ~DnsmasqChild() {
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, nullptr, 0);
        }
    }
    DnsmasqChild(const DnsmasqChild&) = delete;
    DnsmasqChild& operator=(const DnsmasqChild&) = delete;

    pid_t pid() const { return pid_; }
    bool alive() const {
        return pid_ > 0 && ::waitpid(pid_, nullptr, WNOHANG) == 0;
    }
    // True once the stamp is answered (within 2 s) and the child is still alive.
    bool wait_ready() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!alive()) return false;
            const auto r = probe_dns_txt("127.0.0.1", port_, kDnsmasqStampDomain,
                                         std::chrono::milliseconds(200));
            if (r.status == DnsTxtProbeStatus::Ok) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

private:
    pid_t pid_{-1};
    std::uint16_t port_;
};

// Starts the child or returns nullptr (with a MESSAGE) so the caller can skip.
std::unique_ptr<DnsmasqChild> start_child(const std::string& bin, std::uint16_t& port_out) {
    const auto port = free_udp_port();
    if (!port) {
        MESSAGE("no free UDP port: skipped");
        return nullptr;
    }
    port_out = *port;
    auto child = std::make_unique<DnsmasqChild>(bin, *port);
    if (!child->wait_ready()) {
        MESSAGE("dnsmasq could not start: skipped");
        return nullptr;
    }
    return child;
}

} // namespace

TEST_CASE("dnsmasq_real: capability probe") {
    const auto bin = real_dnsmasq();
    if (!bin) {
        MESSAGE("KEEN_PBR_TEST_DNSMASQ not set to an executable: skipped");
        return;
    }
    // dnsmasq drops privileges to "nobody" before reading config unless told
    // otherwise, which fails on a 0700 temp dir owned by a non-root user.
    DnsmasqCapabilityDeps deps;
    const std::string user = current_user();
    deps.exec = [&](const std::vector<std::string>& args, std::chrono::milliseconds t) {
        std::vector<std::string> a = args;
        if (::geteuid() != 0) a.insert(a.begin() + 1, "--user=" + user);
        return dnsmasq_default_timed_exec(a, t);
    };
    const auto r = probe_dnsmasq_capabilities(*bin, deps);
    if (r.status == DnsmasqCapabilityStatus::Failed &&
        r.code == DnsmasqErrorCode::DnsmasqTestFailed) {
        MESSAGE("dnsmasq --test cannot run in this environment (base config fails): skipped: "
                << r.detail);
        return;
    }
    CHECK_MESSAGE(r.status == DnsmasqCapabilityStatus::Ok, r.detail);
}

TEST_CASE("dnsmasq_real: detector verifies the stamp") {
    const auto bin = real_dnsmasq();
    if (!bin) {
        MESSAGE("KEEN_PBR_TEST_DNSMASQ not set to an executable: skipped");
        return;
    }
    std::uint16_t port = 0;
    auto child = start_child(*bin, port);
    if (!child) return;

    DnsmasqDetectorDeps deps;
    const std::string inspect =
        "hook_version=2\nplatform=test\nbinary=" + *bin + "\n"
        "[instance]\nselector=uci:real\nenabled=1\nmain_pid=" + std::to_string(child->pid()) +
        "\ndropin_include=managed\ndropin_dir=/tmp/keen-pbr-real.d\n"
        "dropin_file=/tmp/keen-pbr-real.d/keen-pbr-upstream-dns.conf\n"
        "dropin_content=conf-script=x\ndropin_state=absent\n";
    deps.exec = [inspect](const std::vector<std::string>&) {
        ExecCaptureResult r;
        r.stdout_output = inspect;
        r.exit_code = 0;
        return r;
    };
    DnsmasqDetector det("/fake/dnsmasq-hook.sh", deps);
    DnsmasqDetectOptions opts;
    opts.expected_hash = kStampHash;
    const auto d = det.detect(opts);
    REQUIRE(d.hook_ok);
    const auto* inst = d.selected_instance();
    REQUIRE(inst != nullptr);
    REQUIRE(inst->endpoint.has_value());
    CHECK(inst->endpoint->str() == "127.0.0.1:" + std::to_string(port));
    CHECK(inst->evidence == EndpointEvidence::StampVerified);
    REQUIRE(inst->stamp.has_value());
    CHECK(inst->stamp->hash == kStampHash);
}

TEST_CASE("dnsmasq_real: stamp TXT query") {
    const auto bin = real_dnsmasq();
    if (!bin) {
        MESSAGE("KEEN_PBR_TEST_DNSMASQ not set to an executable: skipped");
        return;
    }
    std::uint16_t port = 0;
    auto child = start_child(*bin, port);
    if (!child) return;

    const auto txt = probe_dns_txt("127.0.0.1", port, kDnsmasqStampDomain,
                                   std::chrono::milliseconds(1000));
    REQUIRE(txt.status == DnsTxtProbeStatus::Ok);
    const auto stamp = parse_dnsmasq_config_stamp(txt.txt);
    REQUIRE(stamp.has_value());
    CHECK(stamp->hash == kStampHash);

    // The generic client sees the same server.
    CHECK(dns_probe_responding("127.0.0.1", port, std::chrono::milliseconds(1000)));
}

} // namespace keen_pbr3
