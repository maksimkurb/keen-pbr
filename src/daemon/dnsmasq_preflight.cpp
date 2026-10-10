#include "dnsmasq_preflight.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace keen_pbr3 {

namespace {

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

struct TempDirGuard {
    std::string path;
    ~TempDirGuard() {
        if (path.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

DnsmasqPreflightResult run_impl(const DnsmasqInstance& inst, const DnsmasqPreflightDeps& deps) {
    DnsmasqPreflightResult res;
    if (inst.hook.preflight_skip) {
        res.ok = true;
        res.skipped = true;
        return res;
    }
    if (!inst.process) {
        res.detail = "dnsmasq is not running";
        return res;
    }

    TempDirGuard guard;
    std::string candidate;
    if (inst.hook.dropin_state == DropinState::Absent) {
        std::string templ = deps.tmp_root + "/keen-pbr-preflight.XXXXXX";
        std::vector<char> buf(templ.begin(), templ.end());
        buf.push_back('\0');
        if (::mkdtemp(buf.data()) == nullptr) {
            res.detail = std::string("mkdtemp: ") + std::strerror(errno);
            return res;
        }
        guard.path = buf.data();
        ::chmod(guard.path.c_str(), 0700);
        candidate = guard.path + "/keen-pbr-upstream-dns.conf";
        {
            std::ofstream out(candidate, std::ios::binary | std::ios::trunc);
            if (!out) {
                res.detail = "cannot write " + candidate;
                return res;
            }
            out << inst.hook.dropin_content << "\n";
            if (!out.flush()) {
                res.detail = "cannot write " + candidate;
                return res;
            }
        }
        ::chmod(candidate.c_str(), 0600);
    }

    res.argv = build_preflight_argv(*inst.process, inst.hook, candidate);
    const DnsmasqTimedExecFn exec = deps.exec ? deps.exec : DnsmasqTimedExecFn(dnsmasq_default_timed_exec);

    const auto t0 = std::chrono::steady_clock::now();
    const ExecCaptureResult r = exec(res.argv, deps.timeout);
    res.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    res.ok = !r.timed_out && r.exit_code == 0;
    if (!res.ok) {
        if (r.timed_out) {
            res.detail = "dnsmasq --test timed out after " +
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(deps.timeout).count()) +
                         " s";
        } else {
            res.detail = dnsmasq_output_tail(r.stdout_output, 512);
        }
    }
    return res;
}

} // namespace

std::vector<std::string> build_preflight_argv(const ProcessInfo& process,
                                              const HookInstanceRecord& hook,
                                              const std::string& candidate_file) {
    std::vector<std::string> out;
    out.push_back(process.exe);
    bool had_conf = false;
    const auto& argv = process.argv;
    std::size_t i = 1;
    while (i < argv.size()) {
        const std::string& a = argv[i];
        if (a == "-k" || a == "--keep-in-foreground" || a == "-d" || a == "--no-daemon" ||
            a == "--test") {
            i += 1;
            continue;
        }
        if (a == "-x" || a == "--pid-file") {
            i += 2;
            continue;
        }
        if (starts_with(a, "-x") || starts_with(a, "--pid-file=")) {
            i += 1;
            continue;
        }
        if ((a == "-C" || a == "--conf-file") && i + 1 < argv.size()) {
            had_conf = true;
            out.push_back(a);
            out.push_back(argv[i + 1]);
            i += 2;
            continue;
        }
        if (starts_with(a, "-C") || starts_with(a, "--conf-file=")) had_conf = true;
        out.push_back(a);
        i += 1;
    }
    if (!had_conf && !hook.default_conf_file.empty()) {
        out.push_back("--conf-file=" + hook.default_conf_file);
    }
    if (!candidate_file.empty()) out.push_back("--conf-file=" + candidate_file);
    out.push_back("--test");
    return out;
}

DnsmasqPreflightResult run_dnsmasq_preflight(const DnsmasqInstance& inst,
                                             const DnsmasqPreflightDeps& deps) {
    try {
        return run_impl(inst, deps);
    } catch (const std::exception& e) {
        DnsmasqPreflightResult r;
        r.detail = dnsmasq_output_tail(e.what(), 512);
        return r;
    } catch (...) {
        DnsmasqPreflightResult r;
        r.detail = "unknown error";
        return r;
    }
}

} // namespace keen_pbr3
