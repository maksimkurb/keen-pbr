#include "dnsmasq_proc.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::optional<std::string> read_link(const std::string& path) {
    char buf[4096];
    const ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (n < 0) return std::nullopt;
    return std::string(buf, static_cast<std::size_t>(n));
}

bool all_digits(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
    }
    return true;
}

bool parse_hex(const std::string& s, unsigned long& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    out = std::strtoul(s.c_str(), nullptr, 16);
    return true;
}

struct DirCloser {
    void operator()(DIR* d) const { if (d) ::closedir(d); }
};

} // namespace

std::optional<ProcessInfo> read_process_info(const std::string& proc_root, pid_t pid) {
    const std::string base = proc_root + "/" + std::to_string(pid);

    auto exe = read_link(base + "/exe");
    if (!exe) return std::nullopt;
    static const std::string kDeleted = " (deleted)";
    if (exe->size() >= kDeleted.size() &&
        exe->compare(exe->size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0) {
        exe->resize(exe->size() - kDeleted.size());
    }

    ProcessInfo info;
    info.pid = pid;
    info.exe = *exe;

    {
        std::ifstream f(base + "/cmdline", std::ios::binary);
        if (!f) return std::nullopt;
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (data.size() > 64 * 1024) data.resize(64 * 1024);
        if (data.empty()) return std::nullopt;
        std::size_t start = 0;
        while (start < data.size()) {
            std::size_t end = data.find('\0', start);
            if (end == std::string::npos) end = data.size();
            info.argv.emplace_back(data.substr(start, end - start));
            start = end + 1;
        }
        // A trailing empty element (from a final '\0') is not produced by the loop above.
    }

    {
        std::ifstream f(base + "/stat");
        std::string line;
        if (f && std::getline(f, line)) {
            const auto rp = line.rfind(')');
            if (rp != std::string::npos) {
                std::istringstream rest(line.substr(rp + 1));
                std::string state, ppid;
                if (rest >> state >> ppid) {
                    char* end = nullptr;
                    const long v = std::strtol(ppid.c_str(), &end, 10);
                    if (end && *end == '\0') info.ppid = static_cast<pid_t>(v);
                }
            }
        }
    }

    info.netns = read_link(base + "/ns/net").value_or("");
    return info;
}

std::vector<ProcessInfo> scan_dnsmasq_processes(const std::string& proc_root) {
    std::vector<ProcessInfo> out;
    std::unique_ptr<DIR, DirCloser> dir(::opendir(proc_root.c_str()));
    if (!dir) return out;
    while (const dirent* e = ::readdir(dir.get())) {
        if (!all_digits(e->d_name)) continue;
        const pid_t pid = static_cast<pid_t>(std::strtol(e->d_name, nullptr, 10));
        auto info = read_process_info(proc_root, pid);
        if (!info) continue;
        const auto slash = info->exe.rfind('/');
        const std::string base = slash == std::string::npos ? info->exe : info->exe.substr(slash + 1);
        if (base.rfind("dnsmasq", 0) != 0) continue;
        out.push_back(std::move(*info));
    }
    std::sort(out.begin(), out.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) { return a.pid < b.pid; });
    return out;
}

std::vector<unsigned long> socket_inodes_of(const std::string& proc_root, pid_t pid) {
    std::vector<unsigned long> out;
    const std::string fd_dir = proc_root + "/" + std::to_string(pid) + "/fd";
    std::unique_ptr<DIR, DirCloser> dir(::opendir(fd_dir.c_str()));
    if (!dir) return out;
    while (const dirent* e = ::readdir(dir.get())) {
        if (e->d_name[0] == '.') continue;
        const auto target = read_link(fd_dir + "/" + e->d_name);
        if (!target) continue;
        static const char kPrefix[] = "socket:[";
        if (target->compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 || target->back() != ']') continue;
        const std::string digits = target->substr(sizeof(kPrefix) - 1, target->size() - sizeof(kPrefix));
        if (digits.empty() || !all_digits(digits.c_str())) continue;
        out.push_back(std::strtoul(digits.c_str(), nullptr, 10));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::optional<DnsmasqEndpoint> parse_proc_net_address(const std::string& hex, int family) {
    const auto colon = hex.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const std::string addr_hex = hex.substr(0, colon);
    const std::string port_hex = hex.substr(colon + 1);

    unsigned long port = 0;
    if (port_hex.empty() || port_hex.size() > 4 || !parse_hex(port_hex, port) || port > 65535) {
        return std::nullopt;
    }

    char buf[INET6_ADDRSTRLEN] = {0};
    if (family == 4) {
        unsigned long v = 0;
        if (addr_hex.size() != 8 || !parse_hex(addr_hex, v)) return std::nullopt;
        in_addr a;
        // The kernel prints the in-memory 32-bit word as a host integer, so
        // assigning it back restores network byte order on any endianness.
        a.s_addr = static_cast<std::uint32_t>(v);
        if (!::inet_ntop(AF_INET, &a, buf, sizeof(buf))) return std::nullopt;
    } else if (family == 6) {
        if (addr_hex.size() != 32) return std::nullopt;
        unsigned char bytes[16];
        for (int i = 0; i < 4; ++i) {
            unsigned long v = 0;
            if (!parse_hex(addr_hex.substr(8 * i, 8), v)) return std::nullopt;
            const std::uint32_t w = static_cast<std::uint32_t>(v);
            std::memcpy(bytes + 4 * i, &w, 4);
        }
        if (!::inet_ntop(AF_INET6, bytes, buf, sizeof(buf))) return std::nullopt;
    } else {
        return std::nullopt;
    }

    DnsmasqEndpoint ep;
    ep.address = buf;
    ep.port = static_cast<std::uint16_t>(port);
    return ep;
}

std::vector<SocketListener> listeners_of(const std::string& proc_root, pid_t pid,
                                         const std::vector<unsigned long>& inodes) {
    struct Src { const char* file; int family; bool tcp; };
    static const Src kSources[] = {
        {"udp", 4, false}, {"udp6", 6, false}, {"tcp", 4, true}, {"tcp6", 6, true},
    };

    std::vector<SocketListener> out;
    const std::string base = proc_root + "/" + std::to_string(pid) + "/net/";

    for (const auto& src : kSources) {
        std::ifstream f(base + src.file);
        if (!f) continue;
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::vector<std::string> tok;
            std::string t;
            while (ss >> t) tok.push_back(t);
            if (tok.size() < 10) continue;
            char* end = nullptr;
            const unsigned long inode = std::strtoul(tok[9].c_str(), &end, 10);
            if (!end || *end != '\0') continue;
            if (std::find(inodes.begin(), inodes.end(), inode) == inodes.end()) continue;
            if (src.tcp && tok[3] != "0A") continue;
            auto ep = parse_proc_net_address(tok[1], src.family);
            if (!ep) continue;

            const bool wildcard = ep->address == "0.0.0.0" || ep->address == "::";
            auto it = std::find_if(out.begin(), out.end(), [&](const SocketListener& l) {
                return l.family == src.family && l.local == *ep;
            });
            if (it == out.end()) {
                SocketListener l;
                l.local = *ep;
                l.family = src.family;
                l.wildcard = wildcard;
                out.push_back(std::move(l));
                it = out.end() - 1;
            }
            if (src.tcp) it->tcp = true; else it->udp = true;
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const SocketListener& a, const SocketListener& b) {
        if (a.family != b.family) return a.family < b.family;
        if (a.wildcard != b.wildcard) return !a.wildcard;
        return a.local.port < b.local.port;
    });
    return out;
}

} // namespace keen_pbr3
