#pragma once

#include "nl_msg.hpp"
#include "nl_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace keen_pbr3::nfnl {

struct NfLogOptions {
    uint16_t group{9054};
    uint32_t copy_range{2048};
    uint32_t queue_threshold{1};
    uint32_t timeout_hundredths{1};
    int rcvbuf_bytes{2 << 20};
};

struct LoggedPacket {
    uint16_t hw_protocol{0};
    uint8_t hook{0};
    uint32_t mark{0};
    uint32_t indev{0};
    uint32_t outdev{0};
    ByteView payload;
    ByteView conntrack_attrs;
};

void build_nflog_bind(MsgBuilder& b, uint32_t seq, uint16_t group);
void build_nflog_unbind(MsgBuilder& b, uint32_t seq, uint16_t group);
void build_nflog_config(MsgBuilder& b, uint32_t seq, const NfLogOptions& options);
bool parse_nflog_packet(const MsgView& message, LoggedPacket& packet);

class NfLog {
public:
    explicit NfLog(const NfLogOptions& options = {});
    ~NfLog();
    NfLog(const NfLog&) = delete;
    NfLog& operator=(const NfLog&) = delete;

    int fd() const { return socket_.fd(); }
    int receive(const std::function<void(const LoggedPacket&)>& on_packet);
    uint64_t overruns() const { return overruns_; }
    uint64_t malformed_packets() const { return malformed_packets_; }
    int last_errno() const { return last_errno_; }

private:
    NfLogOptions options_;
    NlSocket socket_;
    MsgBuilder tx_;
    std::vector<uint8_t> rx_;
    uint64_t overruns_{0};
    uint64_t malformed_packets_{0};
    int last_errno_{0};
    bool bound_{false};
};

} // namespace keen_pbr3::nfnl
