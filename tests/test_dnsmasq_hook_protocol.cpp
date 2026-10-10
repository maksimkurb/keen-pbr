#include "../src/daemon/dnsmasq_hook_protocol.hpp"

#include <doctest/doctest.h>

#include <string>

namespace keen_pbr3 {

namespace {
const char* kExample =
    "hook_version=2\n"
    "platform=openwrt\n"
    "binary=/usr/sbin/dnsmasq\n"
    "[instance]\n"
    "selector=uci:cfg01411c\n"
    "enabled=1\n"
    "match_conf_file=/var/etc/dnsmasq.conf.cfg01411c\n"
    "dropin_include=static\n"
    "dropin_dir=/tmp/dnsmasq.cfg01411c.d\n"
    "dropin_file=/tmp/dnsmasq.cfg01411c.d/keen-pbr-upstream-dns.conf\n"
    "dropin_content=conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq\n"
    "dropin_state=current\n"
    "owned_mounts=/usr/sbin/keen-pbr /etc/keen-pbr /var/cache/keen-pbr\n"
    "missing_mounts=\n";

std::string with_record(const std::string& body) {
    return "hook_version=2\n[instance]\nselector=uci:a\n" + body;
}
}  // namespace

TEST_CASE("dnsmasq_hook_protocol: spec example parses") {
    const auto p = parse_hook_inspect_output(kExample);
    REQUIRE(p.ok);
    CHECK(p.result.hook_version == 2);
    CHECK(p.result.platform == "openwrt");
    CHECK(p.result.binary == "/usr/sbin/dnsmasq");
    REQUIRE(p.result.instances.size() == 1);
    const auto& r = p.result.instances[0];
    CHECK(r.selector == "uci:cfg01411c");
    CHECK(r.enabled);
    CHECK_FALSE(r.main_pid.has_value());
    CHECK(r.match_conf_file == "/var/etc/dnsmasq.conf.cfg01411c");
    CHECK(r.dropin_include == DropinInclude::Static);
    CHECK(r.dropin_dir == "/tmp/dnsmasq.cfg01411c.d");
    CHECK(r.dropin_file == "/tmp/dnsmasq.cfg01411c.d/keen-pbr-upstream-dns.conf");
    CHECK(r.dropin_content == "conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq");
    CHECK(r.dropin_state == DropinState::Current);
    REQUIRE(r.owned_mounts.size() == 3);
    CHECK(r.owned_mounts[1] == "/etc/keen-pbr");
    CHECK(r.missing_mounts.empty());
}

TEST_CASE("dnsmasq_hook_protocol: two instances keep globals") {
    const auto p = parse_hook_inspect_output(
        "hook_version=2\nplatform=debian\nbinary=/usr/sbin/dnsmasq\n"
        "[instance]\nselector=uci:a\n[instance]\nselector=uci:b\nenabled=0\n"
        "exe_required=/opt/sbin/dnsmasq\ndefault_conf_file=/etc/dnsmasq.conf\n"
        "dropin_include=managed\nstatic_endpoint=192.0.2.1:5353\npreflight=skip\n");
    REQUIRE(p.ok);
    CHECK(p.result.platform == "debian");
    REQUIRE(p.result.instances.size() == 2);
    CHECK(p.result.instances[0].selector == "uci:a");
    const auto& b = p.result.instances[1];
    CHECK_FALSE(b.enabled);
    CHECK(b.exe_required == "/opt/sbin/dnsmasq");
    CHECK(b.default_conf_file == "/etc/dnsmasq.conf");
    CHECK(b.dropin_include == DropinInclude::Managed);
    REQUIRE(b.static_endpoint.has_value());
    CHECK(b.static_endpoint->str() == "192.0.2.1:5353");
    CHECK(b.preflight_skip);
}

TEST_CASE("dnsmasq_hook_protocol: CRLF, comments, blanks, unknown keys") {
    const auto p = parse_hook_inspect_output(
        "# comment\r\nhook_version=2\r\n\r\nfuture_key=zzz\r\n[instance]\r\n"
        "selector=uci:a\r\nnew_thing=1\r\n");
    REQUIRE(p.ok);
    REQUIRE(p.result.instances.size() == 1);
    CHECK(p.result.instances[0].selector == "uci:a");
}

TEST_CASE("dnsmasq_hook_protocol: errors") {
    CHECK_FALSE(parse_hook_inspect_output("hook_version=2\nnonsense\n").ok);
    const auto v1 = parse_hook_inspect_output("hook_version=1\n");
    CHECK_FALSE(v1.ok);
    CHECK(v1.error == "unsupported hook_version 1");
    CHECK_FALSE(parse_hook_inspect_output("platform=x\n").ok);
    CHECK_FALSE(parse_hook_inspect_output("hook_version=2\n[instance]\nenabled=1\n").ok);
    CHECK_FALSE(parse_hook_inspect_output(with_record("enabled=2\n")).ok);
    CHECK_FALSE(parse_hook_inspect_output(with_record("dropin_state=weird\n")).ok);
    CHECK_FALSE(parse_hook_inspect_output(with_record("static_endpoint=nonsense\n")).ok);
    CHECK_FALSE(parse_hook_inspect_output(with_record("dropin_include=x\n")).ok);
    CHECK_FALSE(parse_hook_inspect_output(with_record("main_pid=abc\n")).ok);
    const auto e = parse_hook_inspect_output("hook_version=2\nfoo\n");
    CHECK(e.error.find("line 2") != std::string::npos);
}

TEST_CASE("dnsmasq_hook_protocol: main_pid") {
    auto p = parse_hook_inspect_output(with_record("main_pid=0\n"));
    REQUIRE(p.ok);
    CHECK_FALSE(p.result.instances[0].main_pid.has_value());
    p = parse_hook_inspect_output(with_record("main_pid=42\n"));
    REQUIRE(p.ok);
    REQUIRE(p.result.instances[0].main_pid.has_value());
    CHECK(*p.result.instances[0].main_pid == 42);
}

} // namespace keen_pbr3
