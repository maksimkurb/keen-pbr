#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/intercept/intercept_capabilities.hpp"
#include "../src/intercept/intercept_report.hpp"
#include "../src/intercept/intercept_settings.hpp"
#include "probe_fakes.hpp"

#include <algorithm>
#include <map>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::probe_fakes;
using nfnl::ProbeStatus;

namespace {

// Records every request message a probe sends and answers from `script`.
struct Script {
    std::function<Reply(const MsgView&)> answer;
    std::vector<uint16_t> seen;
    std::vector<std::vector<uint8_t>> raw;
    int transports{0};
    bool throw_on_open{false};

    InterceptProbeEnv env() {
        InterceptProbeEnv env;
        env.runtime_probes = true;
        env.modprobe = [](const std::string&) {};
        env.kernel_release = [] { return std::string("4.9.0-test"); };
        env.make_transport = [this]() -> std::unique_ptr<nfnl::SetWriterTransport> {
            ++transports;
            if (throw_on_open) throw nfnl::NlSocketError("netlink socket(): Operation not permitted", EPERM);
            auto transport = std::make_unique<FakeTransport>();
            transport->handler = [this](const MsgView& message) {
                seen.push_back(message.type);
                raw.emplace_back(message.raw.data(), message.raw.data() + message.raw.size());
                return answer ? answer(message) : Reply{};
            };
            return transport;
        };
        return env;
    }
};

Reply healthy_kernel(const MsgView& message) {
    if (message.type == kIpsetProtocolMsg) return Reply{0, 7, false};
    if (message.type == kNftNewElem) return Reply{ENOENT, 0, false};
    return Reply{};
}

InterceptCapabilities all_caps() {
    InterceptCapabilities caps;
    caps.nfqueue = caps.nflog = caps.connbytes = true;
    return caps;
}

Config default_config() { return parse_config("{}"); }

bool contains(const std::vector<std::string>& list, const std::string& needle) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& item) {
        return item.find(needle) != std::string::npos;
    });
}

const InterceptRuntimeProbe::Item* find_item(const std::vector<InterceptRuntimeProbe::Item>& items,
                                             const std::string& feature) {
    for (const auto& item : items) {
        if (item.feature == feature) return &item;
    }
    return nullptr;
}

bool bytes_contain(const std::vector<uint8_t>& haystack, const std::vector<uint8_t>& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

} // namespace

// --- capability-time probes -----------------------------------------------------------

TEST_CASE("intercept probe: nftables backend records nf_tables, conntrack and the kernel release") {
    Script script;
    script.answer = healthy_kernel;
    const auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, script.env());
    CHECK(caps.probe.set_backend.status == ProbeStatus::ok);
    CHECK(caps.probe.conntrack.status == ProbeStatus::ok);
    CHECK(caps.probe.kernel_release == "4.9.0-test");
    CHECK(caps.probe.ipset_protocol == 0);
    // Listener and write results are filled in later stages.
    CHECK(caps.probe.nfqueue.status == ProbeStatus::not_run);
    CHECK(caps.probe.fail_open.status == ProbeStatus::not_run);
    CHECK(caps.probe.nflog.status == ProbeStatus::not_run);
    CHECK(caps.probe.set_write.status == ProbeStatus::not_run);
    REQUIRE(script.seen.size() == 2);
    CHECK(script.seen[0] == kNftNewElem);
    CHECK(script.seen[1] == kCtGet);
}

TEST_CASE("intercept probe: iptables backend asks the ipset protocol") {
    Script script;
    script.answer = healthy_kernel;
    auto env = script.env();
    env.ip_targets = env.ip6_targets = env.ip_matches = env.ip6_matches = "/nonexistent/proc";
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, env);
    CHECK(caps.probe.set_backend.status == ProbeStatus::ok);
    CHECK(caps.probe.ipset_protocol == 7);
    CHECK(script.seen.front() == kIpsetProtocolMsg);
    // /proc and probes are independent: tables are missing, the probes still ran.
    CHECK_FALSE(caps.nfqueue);
    CHECK(caps.probe.conntrack.status == ProbeStatus::ok);
}

TEST_CASE("intercept probe: old ipset protocol disables both parts through the effective settings") {
    Script script;
    script.answer = [](const MsgView& m) {
        return m.type == kIpsetProtocolMsg ? Reply{0, 5, false} : Reply{};
    };
    auto env = script.env();
    env.ip_targets = env.ip6_targets = env.ip_matches = env.ip6_matches = "/nonexistent/proc";
    auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, env);
    CHECK(caps.probe.set_backend.status == ProbeStatus::unsupported);
    CHECK(caps.probe.ipset_protocol == 5);

    caps.nfqueue = caps.nflog = caps.connbytes = true;  // /proc says yes, the probe says no
    const auto eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps);
    CHECK_FALSE(eff.dns_hold);
    CHECK_FALSE(eff.l7);
    CHECK_FALSE(eff.active());
    CHECK(eff.reasons.size() == 2);
    CHECK(contains(eff.reasons, "dns hold disabled: set backend probe failed"));
    CHECK(contains(eff.reasons, "l7 sniffing disabled: set backend probe failed"));
}

TEST_CASE("intercept probe: timeout and refused netlink are errors that block") {
    // No answer at all.
    Script silent;
    silent.answer = [](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
    auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, silent.env());
    CHECK(caps.probe.set_backend.status == ProbeStatus::error);
    CHECK(caps.probe.conntrack.status == ProbeStatus::error);
    auto eff = resolve_effective_intercept(default_config(), FirewallBackend::nftables, [&] {
        auto c = caps;
        c.nfqueue = c.nflog = c.connbytes = true;
        return c;
    }());
    CHECK_FALSE(eff.active());
    CHECK(contains(eff.reasons, "timed out"));

    // nf_tables absent.
    Script refused;
    refused.answer = [](const MsgView& m) {
        return m.type == kNftNewElem ? Reply{EOPNOTSUPP, 0, false} : Reply{};
    };
    caps = probe_intercept_capabilities(FirewallBackend::nftables, true, refused.env());
    CHECK(caps.probe.set_backend.status == ProbeStatus::unsupported);

    // The socket cannot even be opened: reported, never thrown.
    Script no_socket;
    no_socket.throw_on_open = true;
    CHECK_NOTHROW(caps = probe_intercept_capabilities(FirewallBackend::nftables, true,
                                                      no_socket.env()));
    CHECK(caps.probe.set_backend.status == ProbeStatus::error);
    CHECK(caps.probe.set_backend.reason.find("Operation not permitted") != std::string::npos);
    CHECK(caps.probe.conntrack.status == ProbeStatus::error);
}

TEST_CASE("intercept probe: missing ctnetlink only degrades") {
    Script script;
    script.answer = [](const MsgView& m) {
        return m.type == kCtGet ? Reply{EOPNOTSUPP, 0, false} : healthy_kernel(m);
    };
    auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, script.env());
    CHECK(caps.probe.conntrack.status == ProbeStatus::unsupported);
    const auto eff = resolve_effective_intercept(default_config(), FirewallBackend::nftables, caps);
    CHECK(eff.dns_hold);
    CHECK(eff.l7);
    CHECK_FALSE(eff.conntrack_cleanup);
    CHECK(eff.reasons.empty());
    REQUIRE(eff.warnings.size() == 1);
    CHECK(eff.warnings[0].find("conntrack cleanup disabled") != std::string::npos);
}

TEST_CASE("intercept probe: probes can be turned off and stay not_run") {
    Script script;
    auto env = script.env();
    env.runtime_probes = false;
    const auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, env);
    CHECK(caps.probe.set_backend.status == ProbeStatus::not_run);
    CHECK(caps.probe.conntrack.status == ProbeStatus::not_run);
    CHECK(script.transports == 0);
    CHECK(probe_intercept_set_write(FirewallBackend::nftables, {{"kpbr4d_a", FirewallFamily::ipv4, 0}},
                                    env)
              .status == ProbeStatus::not_run);
}

TEST_CASE("intercept probe: load_modules=false skips all modprobe calls") {
    Script script;
    script.answer = healthy_kernel;
    bool modprobe_called = false;
    auto env = script.env();
    env.load_modules = false;
    env.modprobe = [&modprobe_called](const std::string&) { modprobe_called = true; };
    const auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, env);
    // Functional probes still run
    CHECK(caps.probe.set_backend.status == ProbeStatus::ok);
    CHECK(caps.probe.conntrack.status == ProbeStatus::ok);
    // But modprobe was never called
    CHECK_FALSE(modprobe_called);
}

TEST_CASE("intercept probe: load_modules=true (default) calls modprobe") {
    Script script;
    script.answer = healthy_kernel;
    int modprobe_count = 0;
    auto env = script.env();
    env.load_modules = true;
    env.modprobe = [&modprobe_count](const std::string&) { ++modprobe_count; };
    // iptables backend tries to load modules per missing requirement
    env.ip_targets = env.ip6_targets = env.ip_matches = env.ip6_matches = "/nonexistent/proc";
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, env);
    // load_intercept_modules calls modprobe for queue/log modules,
    // then Prober tries modules for missing iptables targets/matches
    CHECK(modprobe_count > 0);
}

// --- set write probe ------------------------------------------------------------------

TEST_CASE("intercept probe: set write is skipped without a dynamic set") {
    Script script;
    CHECK(probe_intercept_set_write(FirewallBackend::nftables, {}, script.env()).status ==
          ProbeStatus::skipped);
    const std::vector<FirewallSetDeclaration> only_static{
        {"kpbr4_a", FirewallFamily::ipv4, 0}, {"kpbr6_a", FirewallFamily::ipv6, 0}};
    const auto result = probe_intercept_set_write(FirewallBackend::iptables, only_static, script.env());
    CHECK(result.status == ProbeStatus::skipped);
    CHECK_FALSE(result.blocks());
    CHECK(script.transports == 0);
}

TEST_CASE("intercept probe: set write targets one of our dynamic sets with a reserved address") {
    // IPv4 preferred when both exist.
    {
        Script script;
        const std::vector<FirewallSetDeclaration> sets{{"kpbr6d_a", FirewallFamily::ipv6, 0},
                                                       {"kpbr4_s", FirewallFamily::ipv4, 0},
                                                       {"kpbr4d_a", FirewallFamily::ipv4, 0}};
        const auto result = probe_intercept_set_write(FirewallBackend::iptables, sets, script.env());
        CHECK(result.status == ProbeStatus::ok);
        REQUIRE(script.seen.size() == 2);
        CHECK(script.seen[0] == kIpsetAddMsg);
        CHECK(script.seen[1] == kIpsetDelMsg);
        const std::string name = "kpbr4d_a";
        CHECK(bytes_contain(script.raw[0], std::vector<uint8_t>(name.begin(), name.end())));
        CHECK(bytes_contain(script.raw[0], {192, 0, 2, 255}));
    }
    // IPv6-only plan.
    {
        Script script;
        const std::vector<FirewallSetDeclaration> sets{{"kpbr6d_a", FirewallFamily::ipv6, 0}};
        CHECK(probe_intercept_set_write(FirewallBackend::nftables, sets, script.env()).status ==
              ProbeStatus::ok);
        REQUIRE(script.seen.size() == 2);
        CHECK(script.seen[0] == kNftNewElem);
        CHECK(script.seen[1] == kNftDelElem);
        CHECK(bytes_contain(script.raw[0], {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}));
    }
}

TEST_CASE("intercept probe: failed set write blocks and is reported with its reason") {
    for (const bool nft : {false, true}) {
        const FirewallBackend backend = nft ? FirewallBackend::nftables : FirewallBackend::iptables;
        const std::vector<FirewallSetDeclaration> sets{{"kpbr4d_a", FirewallFamily::ipv4, 0}};
        const uint16_t add = nft ? kNftNewElem : kIpsetAddMsg;

        Script unsupported;
        unsupported.answer = [add](const MsgView& m) {
            return m.type == add ? Reply{EINVAL, 0, false} : Reply{};
        };
        const auto bad = probe_intercept_set_write(backend, sets, unsupported.env());
        CHECK(bad.status == ProbeStatus::unsupported);
        CHECK(bad.blocks());
        CHECK(bad.reason.find("kpbr4d_a") != std::string::npos);

        Script timeout;
        timeout.answer = [](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
        CHECK(probe_intercept_set_write(backend, sets, timeout.env()).status == ProbeStatus::error);

        Script no_socket;
        no_socket.throw_on_open = true;
        CHECK(probe_intercept_set_write(backend, sets, no_socket.env()).status == ProbeStatus::error);
    }
}

TEST_CASE("intercept probe: nft timeout update runs only for nftables on a dynamic set") {
    const std::vector<FirewallSetDeclaration> sets{{"kpbr4d_a", FirewallFamily::ipv4, 0}};
    {
        Script script;
        CHECK(probe_intercept_nft_timeout_update(FirewallBackend::iptables, sets, script.env())
                  .status == ProbeStatus::not_run);
        CHECK(script.transports == 0);
        CHECK(probe_intercept_nft_timeout_update(FirewallBackend::nftables, {}, script.env())
                  .status == ProbeStatus::skipped);
    }
    {
        Script script;
        script.answer = [](const MsgView& m) {
            return m.type == kNftGetElem ? Reply{0, 0, false, 299000} : Reply{};
        };
        const auto result =
            probe_intercept_nft_timeout_update(FirewallBackend::nftables, sets, script.env());
        CHECK(result.status == ProbeStatus::ok);
        REQUIRE(script.seen.size() == 5);
        // Reserved documentation address 192.0.2.254, distinct from the set_write probe's .255.
        CHECK(bytes_contain(script.raw[1], {192, 0, 2, 254}));
    }
    {
        Script script;  // old kernel keeps the 5 s expiration
        script.answer = [](const MsgView& m) {
            return m.type == kNftGetElem ? Reply{0, 0, false, 3000} : Reply{};
        };
        const auto result =
            probe_intercept_nft_timeout_update(FirewallBackend::nftables, sets, script.env());
        CHECK(result.status == ProbeStatus::unsupported);
        CHECK_FALSE(result.is_ok());
    }
}

// --- effective settings follow the probes ---------------------------------------------

TEST_CASE("effective intercept: a cached listener failure disables only that part") {
    auto caps = all_caps();
    caps.probe.nfqueue = nfnl::make_probe_result(ProbeStatus::error, "queue busy");
    auto eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps);
    CHECK_FALSE(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(contains(eff.reasons, "NFQUEUE bind failed: queue busy"));

    caps = all_caps();
    caps.probe.nflog = nfnl::make_probe_result(ProbeStatus::unsupported, "no nfnetlink_log");
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps);
    CHECK(eff.dns_hold);
    CHECK_FALSE(eff.l7);
    CHECK(contains(eff.reasons, "NFLOG bind failed"));
    REQUIRE(eff.firewall_settings().has_value());
    CHECK(eff.firewall_settings()->dns_hold);
    CHECK_FALSE(eff.firewall_settings()->l7_sniff);

    // A cached set write failure disables both, a skipped one nothing.
    caps = all_caps();
    caps.probe.set_write = nfnl::make_probe_result(ProbeStatus::skipped, "no set");
    CHECK(resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps).active());
    caps.probe.set_write = nfnl::make_probe_result(ProbeStatus::error, "boom");
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps);
    CHECK_FALSE(eff.active());
    CHECK(contains(eff.reasons, "set write probe failed: boom"));
}

TEST_CASE("effective intercept: listener probe results are applied before any rule is planned") {
    InterceptRuntimeProbe listeners;

    // Both listeners bound, kernel accepts fail-open.
    auto eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    listeners.nfqueue = nfnl::make_probe_result(ProbeStatus::ok, "queue bound");
    listeners.fail_open = nfnl::make_probe_result(ProbeStatus::ok);
    listeners.replacement = nfnl::ReplacementCapability::supported;
    listeners.nflog = nfnl::make_probe_result(ProbeStatus::ok, "group bound");
    apply_listener_probe(eff, listeners);
    CHECK(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(eff.warnings.empty());
    CHECK(eff.capabilities.probe.replacement == nfnl::ReplacementCapability::supported);

    // The kernel rejects fail-open: keep running, warn.
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    listeners.fail_open = nfnl::make_probe_result(ProbeStatus::unsupported, "kernel ignores flags");
    apply_listener_probe(eff, listeners);
    CHECK(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(eff.reasons.empty());
    REQUIRE(eff.warnings.size() == 1);
    CHECK(eff.warnings[0].find("fail-open unavailable") != std::string::npos);
    CHECK(eff.warnings[0].find("kernel ignores flags") != std::string::npos);

    // Queue bind fails (EBUSY): DNS off, L7 untouched.
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    InterceptRuntimeProbe queue_busy;
    queue_busy.nfqueue = nfnl::classify_errno(EBUSY, "NFQUEUE bind");
    queue_busy.nflog = nfnl::make_probe_result(ProbeStatus::ok);
    apply_listener_probe(eff, queue_busy);
    CHECK_FALSE(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(contains(eff.reasons, "dns hold disabled: NFQUEUE bind failed"));
    CHECK(eff.firewall_settings().has_value());
    CHECK_FALSE(eff.firewall_settings()->dns_hold);

    // NFLOG bind fails: L7 off, DNS untouched, no warning about fail-open.
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    InterceptRuntimeProbe log_missing;
    log_missing.nfqueue = nfnl::make_probe_result(ProbeStatus::ok);
    log_missing.fail_open = nfnl::make_probe_result(ProbeStatus::ok);
    log_missing.nflog = nfnl::classify_errno(EINVAL, "NFLOG bind");
    apply_listener_probe(eff, log_missing);
    CHECK(eff.dns_hold);
    CHECK_FALSE(eff.l7);
    CHECK(contains(eff.reasons, "l7 sniffing disabled: NFLOG bind failed"));

    // Nothing bound at all: no rule may queue anywhere.
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    InterceptRuntimeProbe none;
    none.nfqueue = nfnl::classify_errno(ETIMEDOUT, "NFQUEUE bind");
    none.nflog = nfnl::classify_errno(ETIMEDOUT, "NFLOG bind");
    apply_listener_probe(eff, none);
    CHECK_FALSE(eff.active());
    CHECK_FALSE(eff.firewall_settings().has_value());

    // not_run members change nothing (the part was not requested).
    eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    apply_listener_probe(eff, InterceptRuntimeProbe{});
    CHECK(eff.dns_hold);
    CHECK(eff.l7);
}

TEST_CASE("effective intercept: set write probe result is applied after the firewall") {
    auto fresh = [] {
        return resolve_effective_intercept(default_config(), FirewallBackend::nftables, all_caps());
    };
    auto eff = fresh();
    apply_set_write_probe(eff, nfnl::make_probe_result(ProbeStatus::ok, "ok"));
    CHECK(eff.active());
    CHECK(eff.capabilities.probe.set_write.status == ProbeStatus::ok);

    eff = fresh();
    apply_set_write_probe(eff, nfnl::make_probe_result(ProbeStatus::skipped, "no dynamic set"));
    CHECK(eff.active());

    for (const auto status : {ProbeStatus::unsupported, ProbeStatus::error}) {
        eff = fresh();
        apply_set_write_probe(eff, nfnl::make_probe_result(status, "element timeout"));
        CHECK_FALSE(eff.active());
        CHECK_FALSE(eff.firewall_settings().has_value());
        CHECK(contains(eff.reasons, "dns hold disabled: set write probe failed: element timeout"));
        CHECK(contains(eff.reasons, "l7 sniffing disabled: set write probe failed: element timeout"));
    }
}

// --- health output ---------------------------------------------------------------------

TEST_CASE("health JSON reports probes, warnings and capability extras") {
    auto caps = all_caps();
    caps.probe.kernel_release = "4.9.0-test";
    caps.probe.ipset_protocol = 7;
    caps.probe.set_backend = nfnl::make_probe_result(ProbeStatus::ok, "ipset protocol 7");
    caps.probe.conntrack = nfnl::make_probe_result(ProbeStatus::unsupported, "ctnetlink: EOPNOTSUPP");
    auto eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, caps);

    InterceptRuntimeProbe listeners;
    listeners.nfqueue = nfnl::make_probe_result(ProbeStatus::ok, "queue bound");
    listeners.fail_open = nfnl::make_probe_result(ProbeStatus::unsupported, "kernel ignores flags");
    listeners.replacement = nfnl::ReplacementCapability::unsupported;
    listeners.nflog = nfnl::classify_errno(EINVAL, "NFLOG bind");
    apply_listener_probe(eff, listeners);
    apply_set_write_probe(eff, nfnl::make_probe_result(ProbeStatus::ok, "element add/delete accepted"));

    const nlohmann::json health = make_intercept_health(eff, /*running=*/true, nullptr, 0);
    CHECK(health["kernel_release"] == "4.9.0-test");
    CHECK(health["ipset_protocol"] == 7);
    CHECK(health["dns_hold_active"] == true);
    CHECK(health["l7_active"] == false);
    CHECK(health["capabilities"]["fail_open"] == false);
    CHECK(health["capabilities"]["payload_replacement"] == "unsupported");
    CHECK(health["capabilities"]["conntrack_cleanup"] == false);
    CHECK(health["warnings"].size() == 2);

    std::map<std::string, nlohmann::json> by_feature;
    for (const auto& probe : health["probes"]) by_feature[probe["feature"]] = probe;
    REQUIRE(by_feature.size() == 8);
    CHECK(by_feature["set_backend"]["status"] == "ok");
    CHECK(by_feature["set_backend"]["reason"] == "ipset protocol 7");
    CHECK(by_feature["nfqueue"]["status"] == "ok");
    CHECK(by_feature["fail_open"]["status"] == "unsupported");
    CHECK(by_feature["fail_open"]["reason"] == "kernel ignores flags");
    CHECK(by_feature["payload_replacement"]["status"] == "unsupported");
    CHECK(by_feature["nflog"]["status"] == "unsupported");
    CHECK(by_feature["set_write"]["status"] == "ok");
    CHECK(by_feature["conntrack"]["status"] == "unsupported");
    CHECK_NOTHROW(health.get<api::InterceptHealthClass>());
}

TEST_CASE("health JSON before any probe leaves optional fields out of the picture") {
    const auto eff = resolve_effective_intercept(default_config(), FirewallBackend::iptables, all_caps());
    const nlohmann::json health = make_intercept_health(eff, false, nullptr, 0);
    CHECK(health["capabilities"]["fail_open"].is_null());
    CHECK(health["capabilities"]["payload_replacement"].is_null());
    CHECK(health["capabilities"]["conntrack_cleanup"].is_null());
    REQUIRE(health["probes"].size() == 8);
    for (const auto& probe : health["probes"]) CHECK(probe["status"] == "not_run");

    // Unknown replacement (pre-4.9, non-initial netns) is its own value.
    auto bound = eff;
    InterceptRuntimeProbe listeners;
    listeners.nfqueue = nfnl::make_probe_result(ProbeStatus::ok);
    listeners.fail_open = nfnl::make_probe_result(ProbeStatus::ok);
    listeners.replacement = nfnl::ReplacementCapability::unknown;
    apply_listener_probe(bound, listeners);
    const nlohmann::json after = make_intercept_health(bound, true, nullptr, 0);
    CHECK(after["capabilities"]["payload_replacement"] == "unknown");
    CHECK(after["capabilities"]["fail_open"] == true);
    std::map<std::string, nlohmann::json> by_feature;
    for (const auto& probe : after["probes"]) by_feature[probe["feature"]] = probe;
    CHECK(by_feature["payload_replacement"]["status"] == "skipped");
}

TEST_CASE("intercept probe: a re-probe carries forward checks it did not run") {
    using nfnl::ProbeStatus;
    InterceptRuntimeProbe previous;
    previous.kernel_release = "6.12.94";
    previous.set_backend = nfnl::make_probe_result(ProbeStatus::ok, "old");
    previous.nfqueue = nfnl::make_probe_result(ProbeStatus::ok, "bound");
    previous.fail_open = nfnl::make_probe_result(ProbeStatus::ok, "accepted");
    previous.replacement = nfnl::ReplacementCapability::supported;
    previous.nflog = nfnl::make_probe_result(ProbeStatus::ok, "bound");
    previous.set_write = nfnl::make_probe_result(ProbeStatus::ok, "written");
    previous.timeout_update = nfnl::make_probe_result(ProbeStatus::ok, "extended");

    // A fresh capability probe only fills its own stage.
    InterceptRuntimeProbe fresh;
    fresh.kernel_release = "6.12.94";
    fresh.set_backend = nfnl::make_probe_result(ProbeStatus::ok, "new");
    fresh.conntrack = nfnl::make_probe_result(ProbeStatus::ok, "dumped");
    fresh.carry_forward(previous);

    CHECK(fresh.set_backend.reason == "new");  // fresh results win
    CHECK(fresh.nfqueue.status == ProbeStatus::ok);
    CHECK(fresh.fail_open.status == ProbeStatus::ok);
    CHECK(fresh.replacement == nfnl::ReplacementCapability::supported);
    CHECK(fresh.nflog.status == ProbeStatus::ok);
    CHECK(fresh.set_write.status == ProbeStatus::ok);
    CHECK(fresh.timeout_update.status == ProbeStatus::ok);
    for (const auto& item : fresh.items()) {
        CAPTURE(item.feature);
        CHECK(item.result.status != ProbeStatus::not_run);
    }
}

TEST_CASE("intercept probe: blocking verdicts are not carried forward") {
    using nfnl::ProbeStatus;
    InterceptRuntimeProbe previous;
    previous.nfqueue = nfnl::make_probe_result(ProbeStatus::error, "bind failed");
    previous.fail_open = nfnl::make_probe_result(ProbeStatus::ok, "accepted");
    previous.nflog = nfnl::make_probe_result(ProbeStatus::error, "bind failed");
    previous.set_write = nfnl::make_probe_result(ProbeStatus::error, "refused");
    REQUIRE(previous.nfqueue.blocks());

    InterceptRuntimeProbe fresh;
    fresh.carry_forward(previous);
    // Retried by the stage that a carried failure would have disabled.
    CHECK(fresh.nfqueue.status == ProbeStatus::not_run);
    CHECK(fresh.fail_open.status == ProbeStatus::not_run);
    CHECK(fresh.nflog.status == ProbeStatus::not_run);
    CHECK(fresh.set_write.status == ProbeStatus::not_run);
}

TEST_CASE("intercept probe: carry_forward edge cases") {
    using nfnl::ProbeStatus;
    InterceptRuntimeProbe previous;
    previous.kernel_release = "6.12.94";
    previous.ipset_protocol = 7;
    previous.nfqueue = nfnl::make_probe_result(ProbeStatus::ok, "bound");
    previous.fail_open = nfnl::make_probe_result(ProbeStatus::unsupported, "old kernel");
    previous.nflog = nfnl::make_probe_result(ProbeStatus::ok, "old bind");
    previous.timeout_update = nfnl::make_probe_result(ProbeStatus::error, "refused");
    REQUIRE(previous.fail_open.blocks());
    REQUIRE(previous.timeout_update.blocks());

    InterceptRuntimeProbe fresh;
    fresh.nflog = nfnl::make_probe_result(ProbeStatus::error, "new bind failed");
    fresh.carry_forward(previous);

    CHECK(fresh.kernel_release == "6.12.94");
    CHECK(fresh.ipset_protocol == 7);
    // fail_open travels with its non-blocking nfqueue, even when it blocks.
    CHECK(fresh.nfqueue.status == ProbeStatus::ok);
    CHECK(fresh.fail_open.status == ProbeStatus::unsupported);
    // A fresh result wins, even a failure over an old success.
    CHECK(fresh.nflog.reason == "new bind failed");
    CHECK(fresh.timeout_update.status == ProbeStatus::not_run);
    // Never measured stays never measured.
    CHECK(fresh.set_write.status == ProbeStatus::not_run);
}
