#include <doctest/doctest.h>

#include "../src/intercept/intercept_service.hpp"
#include "../src/netfilter/kernel_probe.hpp"
#include "../src/netfilter/nfqueue.hpp"
#include "../src/netfilter/nflog.hpp"
#include "../src/netfilter/uapi_compat.hpp"
#include "probe_fakes.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::nfnl;
using namespace keen_pbr3::probe_fakes;

// --- classification ---------------------------------------------------------------

TEST_CASE("probe: errno classification covers ok, unsupported, timeout and error") {
    CHECK(classify_errno(0, "x").status == ProbeStatus::ok);
    for (const int err : {EINVAL, EOPNOTSUPP, ENOSYS, ENOPROTOOPT, EPROTONOSUPPORT, ENOTTY}) {
        CAPTURE(err);
        const auto result = classify_errno(err, "feature");
        CHECK(result.status == ProbeStatus::unsupported);
        CHECK(result.blocks());
        CHECK(result.reason.find("feature") != std::string::npos);
    }
    const auto timeout = classify_errno(ETIMEDOUT, "feature");
    CHECK(timeout.status == ProbeStatus::error);
    CHECK(timeout.reason.find("timed out") != std::string::npos);
    for (const int err : {EPERM, EBUSY, ENOENT, EACCES}) {
        CHECK(classify_errno(err, "feature").status == ProbeStatus::error);
    }
    CHECK_FALSE(make_probe_result(ProbeStatus::skipped).blocks());
    CHECK_FALSE(make_probe_result(ProbeStatus::not_run).blocks());
    CHECK(std::string(probe_status_name(ProbeStatus::unsupported)) == "unsupported");
}

TEST_CASE("probe: fail-open needs the real flag accepted and the control rejected") {
    // Kernel >= 3.6: flag accepted, unknown flag rejected.
    CHECK(classify_fail_open(0, EOPNOTSUPP).status == ProbeStatus::ok);
    CHECK(classify_fail_open(0, EINVAL).status == ProbeStatus::ok);
    // Kernel < 3.6: the attribute is ignored, so the unknown flag is accepted too.
    const auto ignored = classify_fail_open(0, 0);
    CHECK(ignored.status == ProbeStatus::unsupported);
    CHECK(ignored.reason.find("ignores") != std::string::npos);
    // The kernel refused the flag itself.
    CHECK(classify_fail_open(EOPNOTSUPP, EOPNOTSUPP).status == ProbeStatus::unsupported);
    CHECK(classify_fail_open(EINVAL, 0).status == ProbeStatus::unsupported);
    // No ACK or another failure is inconclusive, never "supported".
    CHECK(classify_fail_open(ETIMEDOUT, ETIMEDOUT).status == ProbeStatus::error);
    CHECK(classify_fail_open(0, ETIMEDOUT).status == ProbeStatus::error);
    CHECK(classify_fail_open(EPERM, EPERM).status == ProbeStatus::error);
}

TEST_CASE("probe: nfqueue flag request carries mask and flags only") {
    MsgBuilder builder;
    build_nfqueue_flags(builder, 9, 5, uapi::kNfqaCfgFFailOpen, uapi::kNfqaCfgFFailOpen);
    bool seen = false;
    REQUIRE(for_each_msg(ByteView(builder.data(), builder.size()), [&](const MsgView& message) {
        seen = true;
        CHECK(message.type == ((NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG));
        CHECK(message.res_id == 5);
        CHECK((message.flags & NLM_F_ACK) != 0);
        bool mask = false;
        bool flags = false;
        bool other = false;
        REQUIRE(for_each_attr(message.attrs, [&](const Attr& attr) {
            if (attr.type == uapi::kNfqaCfgMask) mask = attr_u32_be(attr) == 1;
            else if (attr.type == uapi::kNfqaCfgFlags) flags = attr_u32_be(attr) == 1;
            else other = true;
            return true;
        }));
        CHECK(mask);
        CHECK(flags);
        CHECK_FALSE(other);
        return true;
    }));
    CHECK(seen);

    // The bind params message no longer carries the flags when asked not to.
    NfQueueOptions options;
    options.queue_num = 5;
    MsgBuilder with_flags;
    MsgBuilder without_flags;
    build_nfqueue_params(with_flags, 1, options);
    build_nfqueue_params(without_flags, 1, options, /*include_flags=*/false);
    CHECK(without_flags.size() < with_flags.size());
}

TEST_CASE("probe: payload replacement capability has three outcomes") {
    constexpr uint64_t initial_user = 0xEFFFFFFDU;
    constexpr uint64_t initial_net = 0xEFFFFFF9U;
    CHECK(nfqueue_replacement_decision_for_test(7, initial_user, 0) ==
          ReplacementCapability::supported);
    CHECK(nfqueue_replacement_decision_for_test(7, 123, initial_net) ==
          ReplacementCapability::unsupported);
    // No NS_GET_USERNS (pre-4.9): only the initial netns is known good.
    CHECK(nfqueue_replacement_decision_for_test(-1, 0, initial_net) ==
          ReplacementCapability::supported);
    CHECK(nfqueue_replacement_decision_for_test(-1, 0, 123) == ReplacementCapability::unknown);
    CHECK(std::string(replacement_capability_name(ReplacementCapability::unknown)) == "unknown");
}

// --- ipset protocol ---------------------------------------------------------------

TEST_CASE("probe: ipset protocol request and outcomes") {
    FakeTransport transport;
    transport.handler = [](const MsgView& message) {
        CHECK(message.type == kIpsetProtocolMsg);
        return Reply{0, 7, false};
    };
    auto result = probe_ipset_protocol(transport);
    CHECK(result.result.status == ProbeStatus::ok);
    CHECK(result.protocol == 7);
    CHECK(transport.batch_controls == 0);

    transport.handler = [](const MsgView&) { return Reply{0, 6, false}; };
    result = probe_ipset_protocol(transport);
    CHECK(result.result.status == ProbeStatus::ok);
    CHECK(result.protocol == 6);

    // Older than the minimum protocol.
    transport.handler = [](const MsgView&) { return Reply{0, 5, false}; };
    result = probe_ipset_protocol(transport);
    CHECK(result.result.status == ProbeStatus::unsupported);
    CHECK(result.protocol == 5);

    // ACK without a protocol attribute is not trusted.
    transport.handler = [](const MsgView&) { return Reply{}; };
    CHECK(probe_ipset_protocol(transport).result.status == ProbeStatus::error);

    // Kernel without ipset (no subsystem): EINVAL / EOPNOTSUPP.
    for (const int err : {EINVAL, EOPNOTSUPP}) {
        transport.handler = [err](const MsgView&) { return Reply{err, 0, false}; };
        CHECK(probe_ipset_protocol(transport).result.status == ProbeStatus::unsupported);
    }
    transport.handler = [](const MsgView&) { return Reply{EPERM, 0, false}; };
    CHECK(probe_ipset_protocol(transport).result.status == ProbeStatus::error);
    transport.handler = [](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
    CHECK(probe_ipset_protocol(transport).result.status == ProbeStatus::error);
}

// --- nf_tables presence -----------------------------------------------------------

TEST_CASE("probe: nf_tables presence is ENOENT from a batched element request") {
    FakeTransport transport;
    transport.handler = [](const MsgView& message) {
        CHECK(message.type == kNftNewElem);
        return Reply{ENOENT, 0, false};
    };
    CHECK(probe_nft_tables(transport, "KeenPbrTable").status == ProbeStatus::ok);
    CHECK(transport.batch_controls == 2);  // sent as a proper batch

    transport.handler = [](const MsgView&) { return Reply{}; };
    CHECK(probe_nft_tables(transport, "KeenPbrTable").status == ProbeStatus::ok);

    for (const int err : {EOPNOTSUPP, EINVAL}) {
        transport.handler = [err](const MsgView&) { return Reply{err, 0, false}; };
        const auto result = probe_nft_tables(transport, "KeenPbrTable");
        CHECK(result.status == ProbeStatus::unsupported);
        CHECK(result.blocks());
    }
    transport.handler = [](const MsgView&) { return Reply{EPERM, 0, false}; };
    CHECK(probe_nft_tables(transport, "KeenPbrTable").status == ProbeStatus::error);
    transport.handler = [](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
    CHECK(probe_nft_tables(transport, "KeenPbrTable").status == ProbeStatus::error);
}

// --- set write --------------------------------------------------------------------

TEST_CASE("probe: ipset test element is added then deleted") {
    FakeTransport transport;
    transport.handler = [](const MsgView&) { return Reply{}; };
    const auto element = test_element("kpbr4d_a");
    const auto result = probe_set_write(transport, /*nft_backend=*/false, "", element);
    CHECK(result.status == ProbeStatus::ok);
    REQUIRE(transport.types.size() == 2);
    CHECK(transport.types[0] == kIpsetAddMsg);
    CHECK(transport.types[1] == kIpsetDelMsg);
    CHECK(transport.batch_controls == 0);
    // The add is not exclusive, so a leftover element is refreshed, not an error.
    const auto add = transport.raw[0];
    MsgView add_view;
    REQUIRE(for_each_msg(ByteView(add.data(), add.size()), [&](const MsgView& m) {
        add_view = m;
        return false;
    }));
    CHECK((add_view.flags & NLM_F_EXCL) == 0);
}

TEST_CASE("probe: ipset delete golden message has no timeout") {
    MsgBuilder builder;
    build_ipset_del(builder, 3, test_element("kpbr4d_a"));
    REQUIRE(for_each_msg(ByteView(builder.data(), builder.size()), [&](const MsgView& message) {
        CHECK(message.type == kIpsetDelMsg);
        bool has_data = false;
        bool has_name = false;
        REQUIRE(for_each_attr(message.attrs, [&](const Attr& attr) {
            const uint16_t type = attr.type & NLA_TYPE_MASK;
            if (type == IPSET_ATTR_SETNAME) has_name = true;
            if (type == IPSET_ATTR_DATA) {
                has_data = true;
                bool timeout = false;
                (void)for_each_attr(attr.payload, [&](const Attr& inner) {
                    if ((inner.type & NLA_TYPE_MASK) == IPSET_ATTR_TIMEOUT) timeout = true;
                    return true;
                });
                CHECK_FALSE(timeout);
            }
            return true;
        }));
        CHECK(has_data);
        CHECK(has_name);
        return true;
    }));
}

TEST_CASE("probe: nft test element uses batched NEWSETELEM and DELSETELEM") {
    FakeTransport transport;
    transport.handler = [](const MsgView&) { return Reply{}; };
    const auto result =
        probe_set_write(transport, /*nft_backend=*/true, "KeenPbrTable", test_element("kpbr6d_a", 6));
    CHECK(result.status == ProbeStatus::ok);
    REQUIRE(transport.types.size() == 2);
    CHECK(transport.types[0] == kNftNewElem);
    CHECK(transport.types[1] == kNftDelElem);
    CHECK(transport.batch_controls == 4);  // two batches
}

TEST_CASE("probe: set write outcomes for both backends") {
    for (const bool nft : {false, true}) {
        CAPTURE(nft);
        FakeTransport transport;
        const auto element = test_element("kpbr4d_a");
        const uint16_t add_type = nft ? kNftNewElem : kIpsetAddMsg;

        // Kernel rejects the element (set without timeout support, bad key).
        for (const int err : {EINVAL, EOPNOTSUPP}) {
            transport.handler = [&](const MsgView& m) {
                return m.type == add_type ? Reply{err, 0, false} : Reply{};
            };
            const auto result = probe_set_write(transport, nft, "KeenPbrTable", element);
            CHECK(result.status == ProbeStatus::unsupported);
            CHECK(result.reason.find("kpbr4d_a") != std::string::npos);
        }
        // Set vanished / permission / busy: an error, not "unsupported".
        for (const int err : {ENOENT, EPERM, EBUSY}) {
            transport.handler = [&](const MsgView& m) {
                return m.type == add_type ? Reply{err, 0, false} : Reply{};
            };
            CHECK(probe_set_write(transport, nft, "KeenPbrTable", element).status ==
                  ProbeStatus::error);
        }
        // Timeout.
        transport.handler = [&](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
        CHECK(probe_set_write(transport, nft, "KeenPbrTable", element).status ==
              ProbeStatus::error);
        // A failing or already-expired delete does not invalidate a successful add.
        for (const int err : {ENOENT, static_cast<int>(IPSET_ERR_EXIST), EPERM}) {
            transport.handler = [&](const MsgView& m) {
                return m.type == add_type ? Reply{} : Reply{err, 0, false};
            };
            CHECK(probe_set_write(transport, nft, "KeenPbrTable", element).status ==
                  ProbeStatus::ok);
        }
    }
}

// --- ctnetlink --------------------------------------------------------------------

TEST_CASE("probe: ctnetlink dump outcomes") {
    FakeTransport transport;
    transport.handler = [](const MsgView& message) {
        CHECK(message.type == kCtGet);
        CHECK((message.flags & NLM_F_DUMP) == NLM_F_DUMP);
        return Reply{};
    };
    CHECK(probe_ctnetlink(transport).status == ProbeStatus::ok);

    // A big table: data arrived, the dump was still running at the deadline.
    transport.handler = [](const MsgView&) { return Reply{ETIMEDOUT, 0, true}; };
    CHECK(probe_ctnetlink(transport).status == ProbeStatus::ok);

    // Silence is an error.
    transport.handler = [](const MsgView&) { return Reply{ETIMEDOUT, 0, false}; };
    const auto silent = probe_ctnetlink(transport);
    CHECK(silent.status == ProbeStatus::error);
    CHECK(silent.blocks());

    for (const int err : {EINVAL, EOPNOTSUPP}) {
        transport.handler = [err](const MsgView&) { return Reply{err, 0, false}; };
        CHECK(probe_ctnetlink(transport).status == ProbeStatus::unsupported);
    }
    transport.handler = [](const MsgView&) { return Reply{EPERM, 0, false}; };
    CHECK(probe_ctnetlink(transport).status == ProbeStatus::error);
}

// --- live (isolated netns, same gating as test_nfnetfilter_wrappers.cpp) -----------

namespace {

bool isolated_netns_guard() {
    const char* enabled = std::getenv("KPBR_NETLINK_IT");
    const char* guard = std::getenv("KPBR_NETLINK_IN_NETNS");
    const char* parent_id = std::getenv("KPBR_NETLINK_PARENT_NS");
    if (enabled == nullptr || std::string(enabled) != "1" || guard == nullptr ||
        std::string(guard) != "1" || parent_id == nullptr || std::string(parent_id).empty()) {
        return false;
    }
    struct stat self{};
    if (::stat("/proc/self/ns/net", &self) != 0) return false;
    const std::string current = std::to_string(self.st_dev) + ":" + std::to_string(self.st_ino);
    return current != parent_id;
}

bool live_probes_enabled() {
    const char* enabled = std::getenv("KPBR_NFNETLINK_LIVE");
    return enabled != nullptr && std::string(enabled) == "1" && isolated_netns_guard();
}

int shell(const std::string& command) { return std::system(command.c_str()); }

} // namespace

TEST_CASE("kernel probe: live ipset, ctnetlink, queue and log probes (isolated netns only)") {
    if (!live_probes_enabled()) return;

    {
        auto transport = make_probe_transport();
        const auto protocol = probe_ipset_protocol(*transport);
        INFO(protocol.result.reason);
        CHECK(protocol.result.status == ProbeStatus::ok);
        CHECK(protocol.protocol >= 6);
    }
    {
        auto transport = make_probe_transport();
        const auto conntrack = probe_ctnetlink(*transport);
        INFO(conntrack.reason);
        CHECK(conntrack.status == ProbeStatus::ok);
    }

    REQUIRE(shell("ipset create kpbr4d_probe hash:net timeout 0") == 0);
    REQUIRE(shell("ipset create kpbr6d_probe hash:net family inet6 timeout 0") == 0);
    REQUIRE(shell("ipset create kpbr4d_notimeout hash:net") == 0);
    {
        auto transport = make_probe_transport();
        const auto v4 = probe_set_write(*transport, false, "", test_element("kpbr4d_probe"));
        INFO(v4.reason);
        CHECK(v4.status == ProbeStatus::ok);
        auto transport6 = make_probe_transport();
        const auto v6 = probe_set_write(*transport6, false, "", test_element("kpbr6d_probe", 6));
        INFO(v6.reason);
        CHECK(v6.status == ProbeStatus::ok);
        // The reserved addresses are gone again.
        CHECK(shell("ipset test kpbr4d_probe 192.0.2.255 >/dev/null 2>&1") != 0);
        CHECK(shell("ipset test kpbr6d_probe 2001:db8::ffff >/dev/null 2>&1") != 0);
        // A set that cannot hold per-element timeouts is reported, not assumed.
        auto bad = make_probe_transport();
        CHECK(probe_set_write(*bad, false, "", test_element("kpbr4d_notimeout")).blocks());
        // A missing set is an error too.
        auto missing = make_probe_transport();
        CHECK(probe_set_write(*missing, false, "", test_element("kpbr4d_absent")).status ==
              ProbeStatus::error);
    }
    shell("ipset destroy kpbr4d_probe; ipset destroy kpbr6d_probe; ipset destroy kpbr4d_notimeout");

    // Queue: bind, fail-open and replacement are all reported by the kernel.
    {
        NfQueueOptions options;
        options.queue_num = 9153;
        options.fail_open = true;
        NfQueue queue(options);
        INFO(queue.fail_open_probe().reason);
        CHECK(queue.fail_open_probe().status == ProbeStatus::ok);
        CHECK(queue.fail_open_active());
        // A definite answer on kernels with NS_GET_USERNS (4.9+): unsupported when the
        // netns belongs to a user namespace (unshare -Ur), supported for the init owner.
        CHECK(queue.payload_replacement() != ReplacementCapability::unknown);

        NfQueueOptions no_fail_open;
        no_fail_open.queue_num = 9154;
        no_fail_open.fail_open = false;
        NfQueue plain(no_fail_open);
        CHECK(plain.fail_open_probe().status == ProbeStatus::not_run);

        // A second bind of the same queue number reports the failure, with its errno.
        bool threw = false;
        try {
            NfQueue duplicate(options);
        } catch (const NlSocketError& error) {
            threw = true;
            CHECK(error.code() != 0);
            CHECK(classify_errno(error.code(), "NFQUEUE bind").status != ProbeStatus::ok);
        }
        CHECK(threw);
    }
    {
        NfLogOptions options;
        options.group = 9154;
        CHECK_NOTHROW(NfLog(options));
    }
}

TEST_CASE("kernel probe: live nf_tables probes (isolated netns only)") {
    if (!live_probes_enabled()) return;

    // nf_tables answers even before our table exists.
    {
        auto transport = make_probe_transport();
        const auto presence = probe_nft_tables(*transport, "KeenPbrTable");
        INFO(presence.reason);
        CHECK(presence.status == ProbeStatus::ok);
    }

    REQUIRE(shell("nft add table inet KeenPbrTable") == 0);
    REQUIRE(shell("nft add set inet KeenPbrTable kpbr4d_probe '{ type ipv4_addr; flags timeout; }'") ==
            0);
    REQUIRE(shell("nft add set inet KeenPbrTable kpbr6d_probe '{ type ipv6_addr; flags timeout; }'") ==
            0);
    REQUIRE(shell("nft add set inet KeenPbrTable kpbr4d_notimeout '{ type ipv4_addr; }'") == 0);
    {
        auto transport = make_probe_transport();
        const auto v4 = probe_set_write(*transport, true, "KeenPbrTable", test_element("kpbr4d_probe"));
        INFO(v4.reason);
        CHECK(v4.status == ProbeStatus::ok);
        auto transport6 = make_probe_transport();
        const auto v6 =
            probe_set_write(*transport6, true, "KeenPbrTable", test_element("kpbr6d_probe", 6));
        INFO(v6.reason);
        CHECK(v6.status == ProbeStatus::ok);
        CHECK(shell("nft list set inet KeenPbrTable kpbr4d_probe | grep -q 192.0.2.255") != 0);
        CHECK(shell("nft list set inet KeenPbrTable kpbr6d_probe | grep -q 2001:db8::ffff") != 0);
        // Element timeout needs a set created with the timeout flag.
        auto bad = make_probe_transport();
        CHECK(probe_set_write(*bad, true, "KeenPbrTable", test_element("kpbr4d_notimeout"))
                  .blocks());
        auto missing = make_probe_transport();
        CHECK(probe_set_write(*missing, true, "KeenPbrTable", test_element("kpbr4d_absent"))
                  .status == ProbeStatus::error);
    }
    shell("nft delete table inet KeenPbrTable");
}

TEST_CASE("kernel probe: live service binds its listeners independently (isolated netns only)") {
    if (!live_probes_enabled()) return;

    // Another process owns the queue: DNS hold cannot bind, L7 still runs.
    NfQueueOptions holder_options;
    holder_options.queue_num = 9163;
    NfQueue queue_holder(holder_options);
    {
        InterceptService service(make_ipset_writer());
        InterceptServiceOptions options;
        options.queue_num = 9163;
        options.nflog_group = 9164;
        REQUIRE_NOTHROW(service.start(options, nullptr));
        CHECK(service.running());
        CHECK_FALSE(service.dns_bound());
        CHECK(service.l7_bound());
        const auto& probe = service.listener_probe();
        CHECK(probe.nfqueue.blocks());
        CHECK(probe.nflog.status == ProbeStatus::ok);
        CHECK(probe.fail_open.status == ProbeStatus::not_run);
        service.stop();
    }
    // Both taken: start fails, and the reasons are still readable.
    NfLogOptions log_options;
    log_options.group = 9164;
    NfLog log_holder(log_options);
    {
        InterceptService service(make_ipset_writer());
        InterceptServiceOptions options;
        options.queue_num = 9163;
        options.nflog_group = 9164;
        CHECK_THROWS_AS(service.start(options, nullptr), std::runtime_error);
        CHECK_FALSE(service.running());
        CHECK_FALSE(service.dns_bound());
        CHECK_FALSE(service.l7_bound());
        CHECK(service.listener_probe().nfqueue.blocks());
        CHECK(service.listener_probe().nflog.blocks());
    }
    // Free queue: the service reports the kernel's fail-open verdict.
    {
        InterceptService service(make_ipset_writer());
        InterceptServiceOptions options;
        options.queue_num = 9165;
        REQUIRE_NOTHROW(service.start(options, nullptr));
        CHECK(service.dns_bound());
        CHECK_FALSE(service.l7_bound());
        CHECK(service.listener_probe().nfqueue.status == ProbeStatus::ok);
        CHECK(service.listener_probe().fail_open.status == ProbeStatus::ok);
        CHECK(service.listener_probe().nflog.status == ProbeStatus::not_run);
        service.stop();
        CHECK_FALSE(service.dns_bound());
    }
}
