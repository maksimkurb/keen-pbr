#include "dnsmasq_capability.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace keen_pbr3 {

const char* capability_status_name(DnsmasqCapabilityStatus s) {
    switch (s) {
        case DnsmasqCapabilityStatus::NotChecked: return "not_checked";
        case DnsmasqCapabilityStatus::Ok: return "ok";
        case DnsmasqCapabilityStatus::Unsupported: return "unsupported";
        case DnsmasqCapabilityStatus::Failed: return "failed";
    }
    return "not_checked";
}

std::optional<DnsmasqBinaryIdentity> dnsmasq_binary_identity(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    DnsmasqBinaryIdentity id;
    id.path = path;
    id.dev = st.st_dev;
    id.ino = st.st_ino;
    id.size = st.st_size;
#if defined(__APPLE__)
    id.mtime_ns = static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL +
                  static_cast<std::int64_t>(st.st_mtimespec.tv_nsec);
#else
    id.mtime_ns = static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000LL +
                  static_cast<std::int64_t>(st.st_mtim.tv_nsec);
#endif
    return id;
}

ExecCaptureResult dnsmasq_default_timed_exec(const std::vector<std::string>& args,
                                             std::chrono::milliseconds timeout) {
    SafeExecTimeouts t;
    t.timeout = timeout;
    t.kill_grace = std::chrono::seconds{2};
    return safe_exec_capture_with_timeouts(args, t, true, 4096, true);
}

std::string dnsmasq_output_tail(const std::string& text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) return text;
    return text.substr(text.size() - max_bytes);
}

namespace {

struct TempDirGuard {
    std::string path;
    ~TempDirGuard() {
        if (path.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool write_file(const std::string& path, const std::string& content, mode_t mode) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << content;
        if (!out.flush()) return false;
    }
    return ::chmod(path.c_str(), mode) == 0;
}

bool succeeded(const ExecCaptureResult& r) {
    return !r.timed_out && r.exit_code == 0;
}

DnsmasqCapabilityResult probe_impl(const std::string& dnsmasq_path,
                                   const DnsmasqCapabilityDeps& deps) {
    const DnsmasqTimedExecFn exec = deps.exec ? deps.exec : DnsmasqTimedExecFn(dnsmasq_default_timed_exec);

    std::string templ = deps.tmp_root + "/keen-pbr-capprobe.XXXXXX";
    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');
    if (::mkdtemp(buf.data()) == nullptr) {
        return {DnsmasqCapabilityStatus::Failed, DnsmasqErrorCode::DnsmasqTestFailed,
                std::string("mkdtemp: ") + std::strerror(errno)};
    }
    TempDirGuard guard{std::string(buf.data())};
    const std::string& dir = guard.path;
    ::chmod(dir.c_str(), 0700);

    const std::string script =
        "#!/bin/sh\n"
        ": > \"" + dir + "/marker\"\n"
        "echo 'server=/keen-pbr-capability.invalid/127.0.0.1#9'\n"
        "echo 'txt-record=keen-pbr-capability.invalid,ok'\n"
        "echo 'rebind-domain-ok=/keen-pbr-capability.invalid/'\n"
        "echo 'address=/use-application-dns.net/'\n"
        "echo 'no-resolv'\n";
    if (!write_file(dir + "/emit.sh", script, 0700) ||
        !write_file(dir + "/probe.conf", "port=0\nconf-script=" + dir + "/emit.sh\n", 0600) ||
        !write_file(dir + "/base.conf", "port=0\n", 0600)) {
        return {DnsmasqCapabilityStatus::Failed, DnsmasqErrorCode::DnsmasqTestFailed,
                std::string("write probe files: ") + std::strerror(errno)};
    }

    const auto r = exec({dnsmasq_path, "--test", "--conf-file=" + dir + "/probe.conf"},
                        std::chrono::duration_cast<std::chrono::milliseconds>(kCapabilityTimeout));
    std::error_code ec;
    const bool marker = std::filesystem::exists(dir + "/marker", ec);
    if (succeeded(r) && marker) {
        return {DnsmasqCapabilityStatus::Ok, DnsmasqErrorCode::None, ""};
    }
    if (succeeded(r)) {
        return {DnsmasqCapabilityStatus::Unsupported, DnsmasqErrorCode::ConfScriptUnsupported,
                "dnsmasq --test did not run the conf-script"};
    }
    const auto b = exec({dnsmasq_path, "--test", "--conf-file=" + dir + "/base.conf"},
                        std::chrono::duration_cast<std::chrono::milliseconds>(kCapabilityTimeout));
    if (succeeded(b)) {
        return {DnsmasqCapabilityStatus::Unsupported, DnsmasqErrorCode::ConfScriptUnsupported,
                dnsmasq_output_tail(r.stdout_output, 512)};
    }
    return {DnsmasqCapabilityStatus::Failed, DnsmasqErrorCode::DnsmasqTestFailed,
            dnsmasq_output_tail(b.stdout_output, 512)};
}

} // namespace

DnsmasqCapabilityResult probe_dnsmasq_capabilities(const std::string& dnsmasq_path,
                                                   const DnsmasqCapabilityDeps& deps) {
    try {
        return probe_impl(dnsmasq_path, deps);
    } catch (const std::exception& e) {
        return {DnsmasqCapabilityStatus::Failed, DnsmasqErrorCode::DnsmasqTestFailed,
                dnsmasq_output_tail(e.what(), 512)};
    } catch (...) {
        return {DnsmasqCapabilityStatus::Failed, DnsmasqErrorCode::DnsmasqTestFailed, "unknown error"};
    }
}

} // namespace keen_pbr3
