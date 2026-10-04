#pragma once

#include "nl_msg.hpp"

#include <array>
#include <atomic>
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

// Exists is only produced by add_new(): the element was already in the set and
// its timeout was NOT extended.
enum class SetAddResult : uint8_t { Added, Refreshed, Error, Exists };

// Adds host addresses to dynamic firewall sets over nfnetlink (no exec).
// Not thread-safe; use one writer per thread.
class DynamicSetWriter {
public:
    virtual ~DynamicSetWriter() = default;

    // Adds all elements; out[i] receives the result for adds[i].
    // Must complete within timeout_ms or marks the remaining ones Error.
    // Returns false if any element is Error.
    virtual bool add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) = 0;

    // Cheap "is it new?" write for the pre-verdict path: a single exclusive add.
    // Elements that already exist are reported as Exists and are left untouched
    // (no resend, no timeout refresh); everything else behaves as in add().
    // The default falls back to the full add().
    virtual bool add_new(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) {
        return add(adds, out, count, timeout_ms);
    }

    // Extends the timeout of elements believed to exist (post-verdict path); an
    // element that vanished is recreated and reported Added.  Never reports
    // Exists.  Starts with no exclusive probe.  The default falls back to add().
    virtual bool refresh(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) {
        return add(adds, out, count, timeout_ms);
    }

    // nft only: when *flag is true, refresh() extends the timeout of existing
    // elements in place with ONE non-exclusive NEWSETELEM transaction (kernel
    // support is established by probe_nft_timeout_update(); the daemon owns and
    // updates the flag).  Without it (or for permanent elements) refresh() keeps
    // the delete+add path.  Other backends ignore it.
    virtual void set_timeout_update_flag(std::shared_ptr<const std::atomic<bool>> flag) {
        (void)flag;
    }

    // errno of the most recent Error, for logging.
    virtual int last_errno() const = 0;

    // Optional: count writes slower than kSlowWriteMs into *counter (may be
    // null).  The counter must outlive the writer.
    virtual void set_slow_write_counter(std::atomic<uint64_t>* counter) { (void)counter; }

    static constexpr int kSlowWriteMs = 20;
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
    // Microseconds spent in sendto() by transact() calls since the last call
    // (diagnostics only; 0 when the transport does not measure it).
    virtual uint64_t take_send_us() { return 0; }
};

// Production transport over a fresh NETLINK_NETFILTER socket (throws
// NlSocketError on socket failure).  Batch control messages in a request are
// recognised so nf_tables batches are acknowledged correctly.
std::unique_ptr<SetWriterTransport> make_netlink_transport(int rcvbuf_bytes = 1 << 18);

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
// In-place timeout refresh of an existing element: non-exclusive NEWSETELEM
// carrying BOTH NFTA_SET_ELEM_TIMEOUT and NFTA_SET_ELEM_EXPIRATION (ms).  The
// kernel (>= 6.12) only restarts the expiration when the timeout value changes
// or an explicit expiration is given, so the expiration is always sent.
void build_nft_refresh_setelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                               const SetAdd& a);
void build_nft_delsetelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                          const SetAdd& a);

} // namespace keen_pbr3::nfnl
