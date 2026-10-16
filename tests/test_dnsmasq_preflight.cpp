#include "../src/daemon/dnsmasq_preflight.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace keen_pbr3 {

using Strs = std::vector<std::string>;

namespace {

struct TmpRoot {
    std::string path;
    TmpRoot() {
        std::string t = "/tmp/kpbr-pftest.XXXXXX";
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

ProcessInfo proc(const std::string& exe, Strs argv) {
    ProcessInfo p;
    p.pid = 100;
    p.exe = exe;
    p.argv = std::move(argv);
    return p;
}

DnsmasqInstance instance(DropinState state) {
    DnsmasqInstance inst;
    inst.process = proc("/usr/sbin/dnsmasq", {"/usr/sbin/dnsmasq", "-C", "/etc/d.conf", "-k"});
    inst.hook.dropin_state = state;
    inst.hook.dropin_content = "conf-script=/usr/bin/keen-pbr";
    return inst;
}

}  // namespace

TEST_CASE("dnsmasq_preflight: argv OpenWrt") {
    HookInstanceRecord h;
    const auto p = proc("/usr/sbin/dnsmasq", {"/usr/sbin/dnsmasq", "-C", "/var/etc/dnsmasq.conf.cfg", "-k", "-x",
                                              "/var/run/dnsmasq/dnsmasq.cfg.pid"});
    CHECK(build_preflight_argv(p, h, "/tmp/c.conf") ==
          Strs{"/usr/sbin/dnsmasq", "-C", "/var/etc/dnsmasq.conf.cfg", "--conf-file=/tmp/c.conf", "--test"});
}

TEST_CASE("dnsmasq_preflight: argv Debian adds default conf file") {
    HookInstanceRecord h;
    h.default_conf_file = "/etc/dnsmasq.conf";
    const auto p = proc("/usr/sbin/dnsmasq",
                        {"/usr/sbin/dnsmasq", "-x", "/run/dnsmasq/dnsmasq.pid", "-u", "dnsmasq", "-7",
                         "/etc/dnsmasq.d,.dpkg-dist", "--local-service"});
    CHECK(build_preflight_argv(p, h, "") ==
          Strs{"/usr/sbin/dnsmasq", "-u", "dnsmasq", "-7", "/etc/dnsmasq.d,.dpkg-dist", "--local-service",
               "--conf-file=/etc/dnsmasq.conf", "--test"});
}

TEST_CASE("dnsmasq_preflight: argv drops pid-file and foreground flags, keeps -Cfoo") {
    HookInstanceRecord h;
    h.default_conf_file = "/etc/dnsmasq.conf";
    const auto p = proc("/d", {"/d", "--pid-file=/x", "--keep-in-foreground", "-d", "-x/y", "--test", "-Cfoo"});
    CHECK(build_preflight_argv(p, h, "") == Strs{"/d", "-Cfoo", "--test"});
}

TEST_CASE("dnsmasq_preflight: Current drop-in has no candidate") {
    TmpRoot root;
    DnsmasqPreflightDeps deps;
    deps.tmp_root = root.path;
    Strs seen;
    deps.exec = [&](const Strs& a, std::chrono::milliseconds) {
        seen = a;
        ExecCaptureResult r;
        r.exit_code = 0;
        return r;
    };
    const auto r = run_dnsmasq_preflight(instance(DropinState::Current), deps);
    CHECK(r.ok);
    CHECK_FALSE(r.skipped);
    CHECK(seen == Strs{"/usr/sbin/dnsmasq", "-C", "/etc/d.conf", "--test"});
    CHECK(r.argv == seen);
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_preflight: Absent drop-in writes candidate during exec and removes it") {
    TmpRoot root;
    DnsmasqPreflightDeps deps;
    deps.tmp_root = root.path;
    std::string candidate, content;
    deps.exec = [&](const Strs& a, std::chrono::milliseconds) {
        for (const auto& x : a) {
            if (x.rfind("--conf-file=", 0) == 0) candidate = x.substr(12);
        }
        std::ifstream in(candidate);
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
        ExecCaptureResult r;
        r.exit_code = 0;
        return r;
    };
    const auto r = run_dnsmasq_preflight(instance(DropinState::Absent), deps);
    CHECK(r.ok);
    CHECK_FALSE(candidate.empty());
    CHECK(content == "conf-script=/usr/bin/keen-pbr\n");
    CHECK_FALSE(std::filesystem::exists(candidate));
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_preflight: failure carries output tail, timeout has message") {
    TmpRoot root;
    DnsmasqPreflightDeps deps;
    deps.tmp_root = root.path;
    deps.exec = [](const Strs&, std::chrono::milliseconds) {
        ExecCaptureResult r;
        r.exit_code = 1;
        r.stdout_output = "bad option";
        return r;
    };
    auto r = run_dnsmasq_preflight(instance(DropinState::Current), deps);
    CHECK_FALSE(r.ok);
    CHECK(r.detail == "bad option");

    deps.timeout = std::chrono::seconds{7};
    deps.exec = [](const Strs&, std::chrono::milliseconds) {
        ExecCaptureResult r;
        r.timed_out = true;
        return r;
    };
    r = run_dnsmasq_preflight(instance(DropinState::Absent), deps);
    CHECK_FALSE(r.ok);
    CHECK(r.detail == "dnsmasq --test timed out after 7 s");
    CHECK(root.empty());
}

TEST_CASE("dnsmasq_preflight: skip and not running") {
    int calls = 0;
    DnsmasqPreflightDeps deps;
    deps.exec = [&](const Strs&, std::chrono::milliseconds) {
        ++calls;
        return ExecCaptureResult{};
    };
    auto inst = instance(DropinState::Absent);
    inst.hook.preflight_skip = true;
    auto r = run_dnsmasq_preflight(inst, deps);
    CHECK(r.ok);
    CHECK(r.skipped);

    inst.hook.preflight_skip = false;
    inst.process.reset();
    r = run_dnsmasq_preflight(inst, deps);
    CHECK_FALSE(r.ok);
    CHECK(r.detail == "dnsmasq is not running");
    CHECK(calls == 0);
}

}  // namespace keen_pbr3
