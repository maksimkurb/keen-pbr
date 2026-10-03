#pragma once

// Scripted netlink transport for the kernel probe tests: no kernel involved.

#include "../src/netfilter/kernel_probe.hpp"
#include "../src/netfilter/nl_msg.hpp"
#include "../src/netfilter/nl_socket.hpp"
#include "../src/netfilter/set_writer.hpp"
#include "../src/netfilter/uapi_compat.hpp"

#include <cerrno>
#include <functional>
#include <string_view>
#include <vector>

namespace keen_pbr3::probe_fakes {

using namespace keen_pbr3::nfnl;

inline constexpr uint16_t kIpsetProtocolMsg = (NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_PROTOCOL;
inline constexpr uint16_t kIpsetAddMsg = (NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_ADD;
inline constexpr uint16_t kIpsetDelMsg = (NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_DEL;
inline constexpr uint16_t kNftNewElem = (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWSETELEM;
inline constexpr uint16_t kNftDelElem = (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_DELSETELEM;
inline constexpr uint16_t kCtGet = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET;

// What the fake kernel does with one request message.
struct Reply {
    int err{0};             // ACK errno; ETIMEDOUT = no ACK at all
    int protocol{0};        // ipset protocol reply attribute (0 = none)
    bool dump_data{false};  // a dump reply message arrives before the outcome
};

// Scripted transport: no kernel involved.
class FakeTransport final : public SetWriterTransport {
public:
    std::function<Reply(const MsgView&)> handler;
    std::vector<uint16_t> types;           // non-control request messages, in order
    std::vector<std::vector<uint8_t>> raw;  // their raw bytes
    int batch_controls{0};

    uint32_t next_seq() override { return ++seq_; }

    int transact(const uint8_t* request, std::size_t length, uint32_t, uint32_t, int,
                 const std::function<void(const MsgView&)>& on_message,
                 const std::function<void(uint32_t, int)>& on_ack) override {
        int result = 0;
        (void)for_each_msg(ByteView(request, length), [&](const MsgView& message) {
            if (message.type == NFNL_MSG_BATCH_BEGIN || message.type == NFNL_MSG_BATCH_END) {
                ++batch_controls;
                return true;
            }
            types.push_back(message.type);
            raw.emplace_back(message.raw.data(), message.raw.data() + message.raw.size());
            const Reply reply = handler ? handler(message) : Reply{};
            if (reply.protocol != 0 && on_message) {
                MsgBuilder builder;
                builder.begin(kIpsetProtocolMsg, 0, message.seq, AF_UNSPEC, 0);
                builder.put_u8(IPSET_ATTR_PROTOCOL, static_cast<uint8_t>(reply.protocol));
                builder.end();
                (void)for_each_msg(ByteView(builder.data(), builder.size()),
                                   [&](const MsgView& view) {
                                       on_message(view);
                                       return true;
                                   });
            }
            if (reply.dump_data && on_message) on_message(message);
            if (reply.err == ETIMEDOUT) {
                if (result == 0) result = ETIMEDOUT;
                return true;
            }
            if (on_ack) on_ack(message.seq, reply.err);
            if (reply.err != 0 && result == 0) result = reply.err;
            return true;
        });
        return result;
    }

private:
    uint32_t seq_{100};
};

inline SetAdd test_element(std::string_view set, uint8_t family = 4) {
    SetAdd element;
    element.set_name = set;
    element.family = family;
    element.timeout_s = 1;
    if (family == 6) {
        element.addr = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    } else {
        element.addr = {192, 0, 2, 255};
    }
    return element;
}


} // namespace keen_pbr3::probe_fakes
