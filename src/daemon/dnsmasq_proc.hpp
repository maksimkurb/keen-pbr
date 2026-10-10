#pragma once

#include "dnsmasq_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// All functions take `proc_root` ("/proc" in production, a fixture directory in
// tests). None of them throws; unreadable entries are skipped.

// Reads one process. std::nullopt if /proc/<pid> or its exe link is unreadable.
std::optional<ProcessInfo> read_process_info(const std::string& proc_root, pid_t pid);

// Every process whose exe basename starts with "dnsmasq" (covers
// "dnsmasq", "dnsmasq-full"). Sorted by pid.
std::vector<ProcessInfo> scan_dnsmasq_processes(const std::string& proc_root);

// Socket inodes owned by `pid` (from /proc/<pid>/fd/* links "socket:[N]").
std::vector<unsigned long> socket_inodes_of(const std::string& proc_root, pid_t pid);

// Parses /proc/<pid>/net/{udp,udp6,tcp,tcp6} and returns the sockets whose
// inode is in `inodes`. UDP: every socket. TCP: only state 0A (LISTEN).
// Entries with the same address and port are merged (udp=true, tcp=true).
std::vector<SocketListener> listeners_of(const std::string& proc_root, pid_t pid,
                                         const std::vector<unsigned long>& inodes);

// Parses "0100007F:0035" (IPv4) or a 32-hex-digit IPv6 + ":PPPP" as printed by
// the kernel. Exposed for tests.
std::optional<DnsmasqEndpoint> parse_proc_net_address(const std::string& hex, int family);

} // namespace keen_pbr3
