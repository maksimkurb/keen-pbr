#pragma once

#include "nl_msg.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace keen_pbr3::nfnl {

struct SetAdd {
    std::string_view set_name;      // e.g. "kpbr4d_youtube"
    uint8_t family;                 // 4 or 6
    std::array<uint8_t, 16> addr{}; // first 4 bytes used for IPv4
    uint32_t timeout_s{0};          // 0 = permanent element
};

enum class SetAddResult : uint8_t { Added, Refreshed, Error };

// Adds host addresses to dynamic firewall sets over nfnetlink (no exec).
// Not thread-safe; use one writer per thread.
class DynamicSetWriter {
public:
    virtual ~DynamicSetWriter() = default;

    // Adds all elements; out[i] receives the result for adds[i].
    // Must complete within timeout_ms or marks the remaining ones Error.
    // Returns false if any element is Error.
    virtual bool add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) = 0;

    // errno of the most recent Error, for logging.
    virtual int last_errno() const = 0;
};

// Narrow transport seam used by the writer.  The production implementation
// wraps NlSocket; tests can inject a deterministic ACK stream without touching
// the host netfilter state.
class SetWriterTransport {
public:
    virtual ~SetWriterTransport() = default;
    virtual uint32_t next_seq() = 0;
    virtual int transact(const uint8_t* request, std::size_t length,
                         uint32_t first_seq, uint32_t last_seq, int timeout_ms,
                         const std::function<void(const MsgView&)>& on_message,
                         const std::function<void(uint32_t, int)>& on_ack) = 0;
};

// Throw NlSocketError on socket failure.
std::unique_ptr<DynamicSetWriter> make_ipset_writer();
std::unique_ptr<DynamicSetWriter> make_nft_writer(std::string table = "KeenPbrTable");  // family inet

// Test-only construction seam.  The returned writer owns `transport`.
std::unique_ptr<DynamicSetWriter>
make_ipset_writer_for_test(std::unique_ptr<SetWriterTransport> transport);
std::unique_ptr<DynamicSetWriter>
make_nft_writer_for_test(std::string table, std::unique_ptr<SetWriterTransport> transport);

// Exposed for golden tests. `exclusive` sets NLM_F_EXCL so an existing element
// is reported as an error instead of being refreshed.
void build_ipset_add(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive);
void build_nft_newsetelem(MsgBuilder& b, uint32_t seq, std::string_view table, const SetAdd& a,
                          bool exclusive);
void build_nft_delsetelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                          const SetAdd& a);

} // namespace keen_pbr3::nfnl
