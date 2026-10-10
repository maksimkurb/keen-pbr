#include "../src/daemon/dnsmasq_proc.hpp"

#include "proc_fixture.hpp"

#include <doctest/doctest.h>

namespace keen_pbr3 {

using test::ProcFixture;

TEST_CASE("dnsmasq_proc: parse_proc_net_address vectors") {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    CHECK(parse_proc_net_address("0100007F:0035", 4)->str() == "127.0.0.1:53");
    CHECK(parse_proc_net_address("00000000:0035", 4)->str() == "0.0.0.0:53");
    CHECK(parse_proc_net_address("0101A8C0:1538", 4)->str() == "192.168.1.1:5432");
    CHECK(parse_proc_net_address("00000000000000000000000001000000:0035", 6)->str() == "[::1]:53");
#else
    CHECK(parse_proc_net_address("7F000001:0035", 4)->str() == "127.0.0.1:53");
    CHECK(parse_proc_net_address("00000000:0035", 4)->str() == "0.0.0.0:53");
    CHECK(parse_proc_net_address("C0A80101:1538", 4)->str() == "192.168.1.1:5432");
    CHECK(parse_proc_net_address("00000000000000000000000000000001:0035", 6)->str() == "[::1]:53");
#endif
    CHECK_FALSE(parse_proc_net_address("0100007:0035", 4).has_value());
    CHECK_FALSE(parse_proc_net_address("0100007G:0035", 4).has_value());
    CHECK_FALSE(parse_proc_net_address("0100007F", 4).has_value());
    CHECK_FALSE(parse_proc_net_address("0100007F:0035", 6).has_value());
}

TEST_CASE("dnsmasq_proc: scan finds dnsmasq variants only") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq", {"/usr/sbin/dnsmasq", "-C", "/x"});
    fx.add_process(50, "/opt/sbin/dnsmasq-full", {"dnsmasq-full"});
    fx.add_process(200, "/usr/sbin/odhcpd", {"odhcpd"});
    fx.add_process(300, "/usr/sbin/dnsmasq", {});  // empty cmdline
    ProcFixture::mkdirs(fx.root() + "/sys");        // non-numeric entry

    const auto procs = scan_dnsmasq_processes(fx.root());
    REQUIRE(procs.size() == 2);
    CHECK(procs[0].pid == 50);
    CHECK(procs[0].exe == "/opt/sbin/dnsmasq-full");
    CHECK(procs[1].pid == 100);
    CHECK(procs[1].argv.size() == 3);
    CHECK(procs[1].argv[2] == "/x");
    CHECK(procs[1].netns == "net:[4026531840]");
}

TEST_CASE("dnsmasq_proc: stat comm with parenthesis") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq"}, 77, "a) b");
    const auto p = read_process_info(fx.root(), 100);
    REQUIRE(p.has_value());
    CHECK(p->ppid == 77);
}

TEST_CASE("dnsmasq_proc: deleted exe suffix stripped, missing pid is nullopt") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq (deleted)", {"dnsmasq"});
    const auto p = read_process_info(fx.root(), 100);
    REQUIRE(p.has_value());
    CHECK(p->exe == "/usr/sbin/dnsmasq");
    CHECK_FALSE(read_process_info(fx.root(), 999).has_value());
}

TEST_CASE("dnsmasq_proc: socket inodes") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq"});
    fx.add_socket_fd(100, 3, 555);
    fx.add_socket_fd(100, 4, 111);
    fx.add_socket_fd(100, 5, 555);
    ::symlink("/dev/null", (fx.pid_dir(100) + "/fd/6").c_str());
    const auto ino = socket_inodes_of(fx.root(), 100);
    REQUIRE(ino.size() == 2);
    CHECK(ino[0] == 111);
    CHECK(ino[1] == 555);
    CHECK(socket_inodes_of(fx.root(), 999).empty());
}

TEST_CASE("dnsmasq_proc: listeners merge udp and tcp") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq"});
    fx.add_net_row(100, "udp", "00000000:0035", "07", 10);
    fx.add_net_row(100, "tcp", "00000000:0035", "0A", 11);
    fx.add_net_row(100, "tcp", "0100007F:0035", "01", 12);   // not LISTEN
    fx.add_net_row(100, "udp", "0101A8C0:0035", "07", 99);   // foreign inode
    fx.add_net_row(100, "udp6", "00000000000000000000000000000000:0035", "07", 13);

    const auto l = listeners_of(fx.root(), 100, {10, 11, 12, 13});
    REQUIRE(l.size() == 2);
    CHECK(l[0].family == 4);
    CHECK(l[0].local.str() == "0.0.0.0:53");
    CHECK(l[0].wildcard);
    CHECK(l[0].udp);
    CHECK(l[0].tcp);
    CHECK(l[1].family == 6);
    CHECK(l[1].local.str() == "[::]:53");
    CHECK(l[1].wildcard);
    CHECK(l[1].udp);
    CHECK_FALSE(l[1].tcp);
}

TEST_CASE("dnsmasq_proc: listener ordering specific before wildcard") {
    ProcFixture fx;
    fx.add_process(100, "/usr/sbin/dnsmasq", {"dnsmasq"});
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const char* lo = "0100007F:0035";
#else
    const char* lo = "7F000001:0035";
#endif
    fx.add_net_row(100, "udp", "00000000:0035", "07", 10);
    fx.add_net_row(100, "udp", lo, "07", 11);
    const auto l = listeners_of(fx.root(), 100, {10, 11});
    REQUIRE(l.size() == 2);
    CHECK(l[0].local.str() == "127.0.0.1:53");
    CHECK(l[1].wildcard);
}

} // namespace keen_pbr3
