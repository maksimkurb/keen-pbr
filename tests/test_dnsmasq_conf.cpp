#include "../src/daemon/dnsmasq_conf.hpp"

#include "proc_fixture.hpp"

#include <doctest/doctest.h>

#include <algorithm>

namespace keen_pbr3 {

using test::ProcFixture;
using Strs = std::vector<std::string>;

namespace {
struct TmpDir {
    ProcFixture fx;  // reused only for its temp directory lifecycle
    std::string path(const std::string& rel) const { return fx.root() + "/" + rel; }
    void write(const std::string& rel, const std::string& data) const {
        ProcFixture::write_file(path(rel), data);
    }
    void mkdir(const std::string& rel) const { ProcFixture::mkdirs(path(rel)); }
};
bool has(const Strs& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

TEST_CASE("dnsmasq_conf: conf_files_from_argv forms") {
    CHECK(conf_files_from_argv({"dnsmasq", "-C", "a", "-Cb", "--conf-file=c", "--conf-file", "d"}) ==
          Strs{"a", "b", "c", "d"});
    CHECK(conf_files_from_argv({"dnsmasq", "-k"}).empty());
    CHECK(conf_files_from_argv({"dnsmasq", "-C"}).empty());
}

TEST_CASE("dnsmasq_conf: parse_conf_dir_value") {
    const auto s = parse_conf_dir_value("/tmp/d/,*.conf,.bak");
    CHECK(s.dir == "/tmp/d");
    CHECK(s.include_suffixes == Strs{".conf"});
    CHECK(s.exclude_suffixes == Strs{".bak"});
    CHECK(parse_conf_dir_value("/").dir == "/");
}

TEST_CASE("dnsmasq_conf: conf_dir_accepts") {
    const auto inc = parse_conf_dir_value("/d,*.conf");
    CHECK(conf_dir_accepts(inc, "a.conf"));
    CHECK_FALSE(conf_dir_accepts(inc, "a.txt"));
    const auto exc = parse_conf_dir_value("/d,.bak");
    CHECK_FALSE(conf_dir_accepts(exc, "a.bak"));
    CHECK(conf_dir_accepts(exc, "a.conf"));
    CHECK_FALSE(conf_dir_accepts(exc, ".x"));
    CHECK_FALSE(conf_dir_accepts(exc, "x~"));
    CHECK_FALSE(conf_dir_accepts(exc, "#x#"));
    CHECK(conf_dir_accepts(exc, "#x"));
    CHECK_FALSE(conf_dir_accepts(exc, ""));
    const auto ex2 = parse_conf_dir_value("/d,.conf");
    CHECK_FALSE(conf_dir_accepts(ex2, "keen-pbr-upstream-dns.conf"));
}

TEST_CASE("dnsmasq_conf: read_effective_config merges files and argv") {
    TmpDir t;
    t.mkdir("d");
    t.write("main.conf",
            "# comment\n"
            "port=5353\n"
            "conf-dir=" + t.path("d") + ",*.conf\n"
            "listen-address=127.0.0.1,192.168.1.1\n"
            "interface=br-lan # trailing\n"
            "bind-dynamic\n"
            "conf-script=/bin/false\n");
    t.write("d/a.conf", "port=5354\n");
    t.write("d/b.txt", "port=1\n");
    t.write("d/keen-pbr-upstream-dns.conf", "port=9\n");

    const std::string skip = t.path("d/keen-pbr-upstream-dns.conf");
    auto cfg = read_effective_config({"dnsmasq", "-C", t.path("main.conf")}, "", skip);
    REQUIRE(cfg.port.has_value());
    CHECK(*cfg.port == 5354);
    CHECK(cfg.listen_addresses == Strs{"127.0.0.1", "192.168.1.1"});
    CHECK(cfg.interfaces == Strs{"br-lan"});
    CHECK(cfg.bind_dynamic);
    CHECK_FALSE(cfg.bind_interfaces);
    REQUIRE(cfg.conf_dirs.size() == 1);
    CHECK(cfg.conf_dirs[0].dir == t.path("d"));
    CHECK(cfg.conf_files_read.size() == 2);
    CHECK_FALSE(has(cfg.conf_files_read, skip));
    CHECK_FALSE(has(cfg.conf_files_read, t.path("d/b.txt")));

    cfg = read_effective_config({"dnsmasq", "-C", t.path("main.conf"), "--port=5300", "-z", "-I", "wan,lo"}, "", skip);
    REQUIRE(cfg.port.has_value());
    CHECK(*cfg.port == 5300);
    CHECK(cfg.bind_interfaces);
    CHECK(cfg.except_interfaces == Strs{"wan", "lo"});
}

TEST_CASE("dnsmasq_conf: self-including file terminates with warning") {
    TmpDir t;
    t.write("loop.conf", "conf-file=" + t.path("loop.conf") + "\n");
    const auto cfg = read_effective_config({"dnsmasq", "-C", t.path("loop.conf")}, "", "");
    CHECK_FALSE(cfg.warnings.empty());
    CHECK(cfg.conf_files_read.size() <= 6);
}

TEST_CASE("dnsmasq_conf: default conf file used without -C") {
    TmpDir t;
    t.write("def.conf", "port=5300\n");
    auto cfg = read_effective_config({"dnsmasq", "-k"}, t.path("def.conf"), "");
    REQUIRE(cfg.port.has_value());
    CHECK(*cfg.port == 5300);
    cfg = read_effective_config({"dnsmasq", "-C", t.path("missing.conf")}, t.path("def.conf"), "");
    CHECK_FALSE(cfg.port.has_value());
    CHECK_FALSE(cfg.warnings.empty());
}

TEST_CASE("dnsmasq_conf: argv conf-dir files are read") {
    TmpDir t;
    t.mkdir("d");
    t.write("d/x.conf", "interface=eth9\n");
    const auto cfg = read_effective_config({"dnsmasq", "-7", t.path("d") + ",.dpkg-dist"}, "", "");
    CHECK(cfg.interfaces == Strs{"eth9"});
    REQUIRE(cfg.conf_dirs.size() == 1);
}

} // namespace keen_pbr3
