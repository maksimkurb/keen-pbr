#include "../src/daemon/dnsmasq_capability.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace keen_pbr3 {

namespace {

struct TmpRoot {
    std::string path;
    TmpRoot() {
        std::string t = "/tmp/kpbr-captest.XXXXXX";
        std::vector<char> b(t.begin(), t.end());
        b.push_back('\0');
        REQUIRE(::mkdtemp(b.data()) != nullptr);
        path = b.data();
    }
    ~TmpRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    bool empty() const { return std::filesystem::directory_iterator(path) == std::filesystem::directory_iterator(); }
};

std::string read_all(const std::string& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Emulates `dnsmasq --test --conf-file=X`: when the file has a conf-script=
// line and `support_script`, runs it through /bin/sh.
DnsmasqTimedExecFn fake_dnsmasq(bool support_script, int probe_exit, int base_exit, int* calls = nullptr) {
    return [=](const std::vector<std::string>& args, std::chrono::milliseconds) {
        if (calls) ++*calls;
        ExecCaptureResult r;
        std::string conf;
        for (const auto& a : args) {
            if (a.rfind("--conf-file=", 0) == 0) conf = a.substr(12);
        }
        const std::string text = read_all(conf);
        const auto pos = text.find("conf-script=");
        if (pos == std::string::npos) {
            r.exit_code = base_exit;
            r.stdout_output = "base output";
            return r;
        }
        r.exit_code = probe_exit;
        r.stdout_output = "probe output";
        if (support_script) {
            auto end = text.find('\n', pos);
            const std::string script = text.substr(pos + 12, end - pos - 12);
            if (::system(("/bin/sh '" + script + "' >/dev/null").c_str()) != 0) r.exit_code = 1;
        }
        return r;
    };
}

}  // namespace

TEST_CASE("dnsmasq_capability: supported conf-script yields Ok and cleans up") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = fake_dnsmasq(true, 0, 0);
    const auto r = probe_dnsmasq_capabilities("/usr/sbin/dnsmasq", deps);
    CHECK(r.status == DnsmasqCapabilityStatus::Ok);
    CHECK(r.code == DnsmasqErrorCode::None);
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_capability: marker created during exec") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    bool saw_marker = false;
    auto inner = fake_dnsmasq(true, 0, 0);
    deps.exec = [&](const std::vector<std::string>& a, std::chrono::milliseconds t) {
        auto r = inner(a, t);
        for (const auto& x : a) {
            if (x.rfind("--conf-file=", 0) == 0) {
                saw_marker = std::filesystem::exists(
                    std::filesystem::path(x.substr(12)).parent_path() / "marker");
            }
        }
        return r;
    };
    CHECK(probe_dnsmasq_capabilities("dnsmasq", deps).status == DnsmasqCapabilityStatus::Ok);
    CHECK(saw_marker);
}

TEST_CASE("dnsmasq_capability: ignored conf-script is Unsupported") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = fake_dnsmasq(false, 0, 0);
    const auto r = probe_dnsmasq_capabilities("dnsmasq", deps);
    CHECK(r.status == DnsmasqCapabilityStatus::Unsupported);
    CHECK(r.code == DnsmasqErrorCode::ConfScriptUnsupported);
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_capability: probe fails but base works is Unsupported") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = fake_dnsmasq(false, 1, 0);
    const auto r = probe_dnsmasq_capabilities("dnsmasq", deps);
    CHECK(r.status == DnsmasqCapabilityStatus::Unsupported);
    CHECK(r.code == DnsmasqErrorCode::ConfScriptUnsupported);
    CHECK(r.detail == "probe output");
}

TEST_CASE("dnsmasq_capability: both fail is Failed") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = fake_dnsmasq(false, 1, 1);
    const auto r = probe_dnsmasq_capabilities("dnsmasq", deps);
    CHECK(r.status == DnsmasqCapabilityStatus::Failed);
    CHECK(r.code == DnsmasqErrorCode::DnsmasqTestFailed);
    CHECK(r.detail == "base output");
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_capability: timeout is not Ok") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = [](const std::vector<std::string>&, std::chrono::milliseconds) {
        ExecCaptureResult r;
        r.timed_out = true;
        r.exit_code = -1;
        return r;
    };
    const auto r = probe_dnsmasq_capabilities("dnsmasq", deps);
    CHECK(r.status != DnsmasqCapabilityStatus::Ok);
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_capability: detail is bounded and mkdtemp failure reported") {
    TmpRoot root;
    DnsmasqCapabilityDeps deps;
    deps.tmp_root = root.path;
    deps.exec = [](const std::vector<std::string>&, std::chrono::milliseconds) {
        ExecCaptureResult r;
        r.exit_code = 1;
        r.stdout_output = std::string(2000, 'x');
        return r;
    };
    CHECK(probe_dnsmasq_capabilities("dnsmasq", deps).detail.size() == 512);

    deps.tmp_root = root.path + "/missing";
    const auto r = probe_dnsmasq_capabilities("dnsmasq", deps);
    CHECK(r.status == DnsmasqCapabilityStatus::Failed);
    CHECK(r.code == DnsmasqErrorCode::DnsmasqTestFailed);
}

TEST_CASE("dnsmasq_capability: binary identity") {
    TmpRoot root;
    const std::string p = root.path + "/bin";
    { std::ofstream(p) << "abc"; }
    auto a = dnsmasq_binary_identity(p);
    REQUIRE(a);
    CHECK(a->size == 3);
    { std::ofstream(p) << "abcdef"; }
    auto b = dnsmasq_binary_identity(p);
    REQUIRE(b);
    CHECK(*a != *b);
    CHECK_FALSE(dnsmasq_binary_identity(root.path + "/none"));
    CHECK(std::string(capability_status_name(DnsmasqCapabilityStatus::Unsupported)) == "unsupported");
}

}  // namespace keen_pbr3
