#pragma once

// Builds a fake /proc tree in a temp directory for dnsmasq detection tests.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace keen_pbr3::test {

class ProcFixture {
public:
    ProcFixture() {
        char tmpl[] = "/tmp/kpbr-proc-XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        root_ = d ? d : "";
    }
    ~ProcFixture() {
        if (!root_.empty()) {
            const std::string cmd = "rm -rf '" + root_ + "'";
            [[maybe_unused]] int rc = std::system(cmd.c_str());
        }
    }
    ProcFixture(const ProcFixture&) = delete;
    ProcFixture& operator=(const ProcFixture&) = delete;

    const std::string& root() const { return root_; }

    // Creates <pid>/{exe,cmdline,stat,ns/net,fd,net/*}. argv empty -> empty cmdline.
    void add_process(int pid, const std::string& exe, const std::vector<std::string>& argv,
                     int ppid = 1, const std::string& comm = "dnsmasq",
                     const std::string& netns = "net:[4026531840]") {
        const std::string base = pid_dir(pid);
        mkdirs(base + "/ns");
        mkdirs(base + "/fd");
        mkdirs(base + "/net");
        ::unlink((base + "/exe").c_str());
        [[maybe_unused]] int rc = ::symlink(exe.c_str(), (base + "/exe").c_str());
        std::string cmd;
        for (const auto& a : argv) { cmd += a; cmd.push_back('\0'); }
        write_file(base + "/cmdline", cmd);
        write_file(base + "/stat", std::to_string(pid) + " (" + comm + ") S " + std::to_string(ppid) + " 1 1 0 -1\n");
        ::unlink((base + "/ns/net").c_str());
        rc = ::symlink(netns.c_str(), (base + "/ns/net").c_str());
        for (const char* f : {"udp", "udp6", "tcp", "tcp6"}) {
            if (!file_exists(base + "/net/" + f)) write_file(base + "/net/" + f, kHeader);
        }
        mkdirs(root_ + "/self/ns");
        ::unlink((root_ + "/self/ns/net").c_str());
        rc = ::symlink(netns.c_str(), (root_ + "/self/ns/net").c_str());
    }

    // Makes <pid>/fd/<n> a link to socket:[inode].
    void add_socket_fd(int pid, int fd, unsigned long inode) {
        const std::string link = pid_dir(pid) + "/fd/" + std::to_string(fd);
        ::unlink(link.c_str());
        [[maybe_unused]] int rc = ::symlink(("socket:[" + std::to_string(inode) + "]").c_str(), link.c_str());
    }

    // Appends a row to <pid>/net/<file> (file: udp, udp6, tcp, tcp6).
    // local is the raw kernel text, e.g. "00000000:0035"; state is hex, e.g. "07" or "0A".
    void add_net_row(int pid, const std::string& file, const std::string& local,
                     const std::string& state, unsigned long inode) {
        const std::string path = pid_dir(pid) + "/net/" + file;
        std::ofstream f(path, std::ios::app);
        f << "   0: " << local << " 00000000:0000 " << state
          << " 00000000:00000000 00:00000000 00000000     0        0 "
          << inode << " 2 0000000000000000 0\n";
    }

    std::string pid_dir(int pid) const { return root_ + "/" + std::to_string(pid); }

    static void mkdirs(const std::string& p) {
        std::string cur;
        for (std::size_t i = 0; i <= p.size(); ++i) {
            if (i == p.size() || p[i] == '/') {
                if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
            }
            if (i < p.size()) cur.push_back(p[i]);
        }
    }
    static void write_file(const std::string& path, const std::string& data) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << data;
    }
    static bool file_exists(const std::string& p) {
        struct stat st;
        return ::stat(p.c_str(), &st) == 0;
    }

private:
    static constexpr const char* kHeader =
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";
    std::string root_;
};

} // namespace keen_pbr3::test
