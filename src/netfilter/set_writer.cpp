#include "set_writer.hpp"

#include "nl_socket.hpp"
#include "uapi_compat.hpp"


#include "../log/logger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <vector>

namespace keen_pbr3::nfnl {

namespace {

// Maximum elements per datagram; keeps requests far below the socket buffer.
constexpr std::size_t kMaxPerDatagram = 128;

// Oldest protocol version kernels accept. Current userspace sends 7, but 6 is
// understood by old (Keenetic) and new kernels alike and is all we need.
constexpr uint8_t kIpsetProtocol = IPSET_PROTOCOL_MIN;

constexpr int kErrIpsetExist = IPSET_ERR_EXIST;
constexpr int kPending = -1;  // no ack received yet

int remaining_ms(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::max<long long>(ms, 1));
}

bool is_exist(int e) { return e == EEXIST || e == kErrIpsetExist; }

class NlSocketTransport final : public SetWriterTransport {
public:
    explicit NlSocketTransport(int rcvbuf) : socket_(rcvbuf) {}

    uint32_t next_seq() override { return socket_.next_seq(); }
    uint64_t take_send_us() override { return socket_.take_send_us(); }

    int transact(const uint8_t* request, std::size_t length, uint32_t first_seq,
                 uint32_t last_seq, int timeout_ms,
                 const std::function<void(const MsgView&)>& on_message,
                 const std::function<void(uint32_t, int)>& on_ack) override {
        std::vector<uint32_t> batch_controls;
        (void)for_each_msg(ByteView(request, length), [&](const MsgView& message) {
            if (message.type == NFNL_MSG_BATCH_BEGIN || message.type == NFNL_MSG_BATCH_END) {
                batch_controls.push_back(message.seq);
            }
            return true;
        });
        return socket_.transact(request, length, first_seq, last_seq, timeout_ms,
                                on_message, on_ack, &batch_controls);
    }

private:
    NlSocket socket_;
};

// Shared two-pass logic; backends supply message construction and, where
// needed, an atomic delete+add refresh for an existing element.
class WriterBase : public DynamicSetWriter {
public:
    WriterBase(bool batched, std::unique_ptr<SetWriterTransport> transport)
        : batched_(batched), transport_(std::move(transport)) {}

    int last_errno() const override { return last_errno_; }

    void set_slow_write_counter(std::atomic<uint64_t>* counter) override { slow_counter_ = counter; }

    bool add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::Full, adds, out, count, timeout_ms);
    }
    bool add_new(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::NewOnly, adds, out, count, timeout_ms);
    }
    bool refresh(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::Existing, adds, out, count, timeout_ms);
    }

protected:
    virtual void build(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) = 0;
    virtual bool supports_refresh() const { return false; }
    virtual void build_refresh(MsgBuilder&, uint32_t, uint32_t, const SetAdd&) {}
    // True when refresh() may extend timeouts in place (one NEWSETELEM each).
    virtual bool in_place_refresh() const { return false; }
    virtual void build_in_place(MsgBuilder&, uint32_t, const SetAdd&) {}

private:
    // Full: exclusive add, then resend/refresh of what existed (add()).
    // NewOnly: exclusive add only; existing elements -> Exists (add_new()).
    // Existing: skip the exclusive probe, treat every element as existing (refresh()).
    enum class Mode : uint8_t { Full, NewOnly, Existing };

    bool timed_add(Mode mode, const SetAdd* adds, SetAddResult* out, std::size_t count,
                   int timeout_ms) {
        if (count == 0) return true;
        (void)transport_->take_send_us();
        const auto started = std::chrono::steady_clock::now();
        const bool ok = add_impl(mode, adds, out, count, timeout_ms);
        const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
        if (total_us >= static_cast<long long>(kSlowWriteMs) * 1000) {
            note_slow_write(static_cast<uint64_t>(total_us), transport_->take_send_us(), count,
                            adds != nullptr ? adds[0].set_name : std::string_view());
        }
        return ok;
    }

    void note_slow_write(uint64_t total_us, uint64_t send_us, std::size_t batch,
                         std::string_view set_name) {
        if (slow_counter_ != nullptr) slow_counter_->fetch_add(1, std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        if (logged_slow_ && now - last_slow_log_ < std::chrono::seconds(10)) return;
        logged_slow_ = true;
        last_slow_log_ = now;
        const uint64_t ack_us = total_us > send_us ? total_us - send_us : 0;
        Logger::instance().info(
            "intercept: slow set write {} us (send {} us, ack {} us, batch {} elems, set {})",
            total_us, send_us, ack_us, batch, set_name);
    }

    bool add_impl(Mode mode, const SetAdd* adds, SetAddResult* out, std::size_t count,
                  int timeout_ms) {
        if (adds == nullptr || out == nullptr) {
            last_errno_ = EINVAL;
            if (out != nullptr) std::fill(out, out + count, SetAddResult::Error);
            return false;
        }
        if (timeout_ms <= 0) {
            last_errno_ = ETIMEDOUT;
            std::fill(out, out + count, SetAddResult::Error);
            return false;
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        last_errno_ = 0;
        bool ok = true;
        for (std::size_t base = 0; base < count; base += kMaxPerDatagram) {
            const std::size_t n = std::min(kMaxPerDatagram, count - base);
            if (remaining_ms(deadline) == 0) {
                std::fill(out + base, out + base + n, SetAddResult::Error);
                last_errno_ = ETIMEDOUT;
                ok = false;
                continue;
            }
            ok = add_chunk(mode, adds + base, out + base, n, deadline) && ok;
        }
        return ok;
    }

    // Sends adds[sel[0..m)] in one datagram and stores each ack's errno in errs[sel[i]].
    int run_pass(const SetAdd* adds, const std::size_t* sel, std::size_t m, bool exclusive,
                 int* errs, std::chrono::steady_clock::time_point deadline,
                 bool in_place = false) {
        b_.clear();
        if (remaining_ms(deadline) == 0) {
            for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = ETIMEDOUT;
            transport_errno_ = ETIMEDOUT;
            return ETIMEDOUT;
        }
        if (batched_) b_.batch_begin(transport_->next_seq());
        const uint32_t first_seq = transport_->next_seq();
        const auto emit = [&](uint32_t seq, const SetAdd& a) {
            if (in_place) {
                build_in_place(b_, seq, a);
            } else {
                build(b_, seq, a, exclusive);
            }
        };
        emit(first_seq, adds[sel[0]]);
        for (std::size_t i = 1; i < m; ++i) emit(transport_->next_seq(), adds[sel[i]]);
        const uint32_t last_seq = first_seq + static_cast<uint32_t>(m) - 1;
        if (batched_) b_.batch_end(transport_->next_seq());

        struct Ctx {
            uint32_t first_seq;
            std::size_t m;
            const std::size_t* sel;
            int* errs;
            std::vector<uint8_t>* seen;
        };
        ack_seen_.assign(m, 0);
        Ctx ctx{first_seq, m, sel, errs, &ack_seen_};
        Ctx* c = &ctx;  // single-pointer capture keeps std::function allocation-free

        const int timeout = remaining_ms(deadline);
        if (timeout == 0) {
            for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = ETIMEDOUT;
            transport_errno_ = ETIMEDOUT;
            return ETIMEDOUT;
        }
        const int rc = transport_->transact(
            b_.data(), b_.size(), first_seq, last_seq, timeout, nullptr,
            [c](uint32_t seq, int err) {
                const uint32_t i = seq - c->first_seq;
                if (i < c->m) {
                    (*c->seen)[i] = 1;
                    c->errs[c->sel[i]] = err;
                }
            });
        if (rc != 0) transport_errno_ = rc;
        return rc;
    }

    int run_refresh(const SetAdd* adds, const std::size_t* sel, std::size_t m,
                    int* errs, std::chrono::steady_clock::time_point deadline) {
        if (remaining_ms(deadline) == 0) {
            for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = ETIMEDOUT;
            transport_errno_ = ETIMEDOUT;
            return ETIMEDOUT;
        }
        b_.clear();
        if (batched_) b_.batch_begin(transport_->next_seq());
        const uint32_t first_seq = transport_->next_seq();
        for (std::size_t i = 0; i < m; ++i) {
            const uint32_t del_seq = i == 0 ? first_seq : transport_->next_seq();
            const uint32_t add_seq = transport_->next_seq();
            build_refresh(b_, del_seq, add_seq, adds[sel[i]]);
        }
        const uint32_t last_seq = first_seq + static_cast<uint32_t>(m * 2) - 1;
        if (batched_) b_.batch_end(transport_->next_seq());

        struct Ctx {
            uint32_t first_seq;
            std::size_t m;
            const std::size_t* sel;
            int* errs;
            std::vector<uint8_t>* seen;
        };
        refresh_seen_.assign(m * 2, 0);
        Ctx ctx{first_seq, m, sel, errs, &refresh_seen_};
        const int timeout = remaining_ms(deadline);
        if (timeout == 0) {
            for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = ETIMEDOUT;
            transport_errno_ = ETIMEDOUT;
            return ETIMEDOUT;
        }
        const int rc = transport_->transact(
            b_.data(), b_.size(), first_seq, last_seq, timeout, nullptr,
            [&ctx](uint32_t seq, int err) {
                const uint32_t offset = seq - ctx.first_seq;
                if (offset >= ctx.m * 2) return;
                (*ctx.seen)[offset] = 1;
                const std::size_t item = offset / 2;
                if (err != 0) {
                    ctx.errs[ctx.sel[item]] = err;
                }
            });
        if (rc != 0) {
            transport_errno_ = rc;
            // A batch-level error rolls back every element, even when the
            // kernel happened to ACK some inner messages before reporting it.
            for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = rc;
        }
        for (std::size_t i = 0; i < m; ++i) {
            const bool complete = refresh_seen_[i * 2] != 0 && refresh_seen_[i * 2 + 1] != 0;
            if (errs[sel[i]] == kPending) {
                errs[sel[i]] = complete ? 0 : (rc == 0 ? ETIMEDOUT : rc);
            }
        }
        return rc;
    }

    // refresh() with kernel support: one non-exclusive NEWSETELEM transaction
    // restarts the timeout of every element (an element that vanished is simply
    // created by the same message, which cannot be told apart from an update).
    // A transaction error rolls the whole batch back, so every element then
    // carries that error.
    bool refresh_in_place(const SetAdd* adds, SetAddResult* out, std::size_t n,
                          std::chrono::steady_clock::time_point deadline) {
        all_.resize(n);
        for (std::size_t i = 0; i < n; ++i) all_[i] = i;
        errs_.assign(n, kPending);
        transport_errno_ = ETIMEDOUT;
        const int rc = run_pass(adds, all_.data(), n, /*exclusive=*/false, errs_.data(), deadline,
                                /*in_place=*/true);
        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            int err = errs_[i];
            if (rc != 0) err = rc;
            if (err == kPending) err = ETIMEDOUT;
            if (err == 0) {
                out[i] = SetAddResult::Refreshed;
            } else {
                out[i] = SetAddResult::Error;
                last_errno_ = err;
                ok = false;
            }
        }
        return ok;
    }

    bool add_chunk(Mode mode, const SetAdd* adds, SetAddResult* out, std::size_t n,
                   std::chrono::steady_clock::time_point deadline) {
        if (mode == Mode::Existing && in_place_refresh() &&
            std::all_of(adds, adds + n, [](const SetAdd& a) { return a.timeout_s != 0; })) {
            return refresh_in_place(adds, out, n, deadline);
        }
        all_.resize(n);
        for (std::size_t i = 0; i < n; ++i) all_[i] = i;
        errs_.assign(n, kPending);
        transport_errno_ = ETIMEDOUT;
        recreated_.clear();

        int first_rc = 0;
        if (mode == Mode::Existing) {
            // refresh(): the caller believes every element exists, so there is
            // no exclusive probe; go straight to the non-exclusive resend.
            errs_.assign(n, EEXIST);
            ack_seen_.assign(n, 0);
        } else {
            first_rc = run_pass(adds, all_.data(), n, /*exclusive=*/true, errs_.data(), deadline);
        }

        // Existing elements are refreshed by a non-exclusive resend. An nft
        // batch is one transaction: any error aborts it and rolls back its
        // other elements even though they were acked, so every acknowledged
        // element is resent after a failed batch.
        bool any_error = first_rc != 0;
        for (std::size_t i = 0; i < n; ++i) any_error = any_error || errs_[i] != 0;
        resend_.clear();
        for (std::size_t i = 0; i < n; ++i) {
            // add_new() leaves existing elements alone (reported Exists); it
            // only re-sends elements an aborted nft batch rolled back.
            if ((mode != Mode::NewOnly && is_exist(errs_[i])) ||
                (batched_ && any_error && errs_[i] == 0 && ack_seen_[i])) {
                resend_.push_back(i);
            }
        }
        first_.assign(errs_.begin(), errs_.end());
        int second_rc = 0;
        if (!resend_.empty() && remaining_ms(deadline) != 0) {
            for (const std::size_t i : resend_) errs_[i] = kPending;
            second_rc = run_pass(adds, resend_.data(), resend_.size(),
                                 /*exclusive=*/false, errs_.data(), deadline);
            if (second_rc != 0) {
                // A transport/batch error means no resend result is
                // publishable.  For nft this is the atomic-batch rollback;
                // for ipset it covers a failed second transaction even when
                // a test transport happened to report an inner ACK first.
                for (const std::size_t i : resend_) errs_[i] = second_rc;
            }
        } else if (!resend_.empty()) {
            for (const std::size_t i : resend_) errs_[i] = ETIMEDOUT;
            transport_errno_ = ETIMEDOUT;
        }

        refresh_.clear();
        if (mode != Mode::NewOnly && supports_refresh()) {
            for (const std::size_t i : resend_) {
                if (second_rc == 0 && is_exist(first_[i]) && errs_[i] == 0) {
                    refresh_.push_back(i);
                }
            }
            if (!refresh_.empty()) {
                for (const std::size_t i : refresh_) errs_[i] = kPending;
                const int refresh_rc = run_refresh(
                    adds, refresh_.data(), refresh_.size(), errs_.data(), deadline);
                // An element may expire between the EEXIST probe and the
                // delete+add refresh.  Retry those elements individually:
                // an exclusive add recreates an expired element, while a
                // renewed EEXIST is followed by one bounded single-element
                // delete+add attempt.  Never turn a partial batch ACK into a
                // success; only complete per-element passes may do that.
                if (refresh_rc == ENOENT || refresh_rc == EAGAIN ||
                    std::any_of(refresh_.begin(), refresh_.end(), [&](std::size_t i) {
                        return errs_[i] == ENOENT || errs_[i] == EAGAIN;
                    })) {
                    for (const std::size_t i : refresh_) {
                        if (errs_[i] != ENOENT && errs_[i] != EAGAIN) continue;
                        const std::size_t one = i;
                        errs_[i] = kPending;
                        const int recreate_rc = run_pass(
                            adds, &one, 1, /*exclusive=*/true, errs_.data(), deadline);
                        if (recreate_rc != 0 && !is_exist(recreate_rc)) {
                            if (errs_[i] == 0 || errs_[i] == kPending) {
                                errs_[i] = recreate_rc;
                            }
                            continue;
                        }
                        if (recreate_rc == 0 && errs_[i] == 0) {
                            // The old element expired and this exclusive
                            // pass created a new one; report that fact rather
                            // than calling a delete+add refresh.
                            recreated_.push_back(i);
                            continue;
                        }
                        if (!is_exist(errs_[i])) continue;
                        errs_[i] = kPending;
                        (void)run_refresh(adds, &one, 1, errs_.data(), deadline);
                    }
                }
            }
        }

        bool ok = true;
        std::size_t next_resend = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const bool resent = next_resend < resend_.size() && resend_[next_resend] == i;
            if (resent) ++next_resend;
            const int e1 = first_[i];
            const int err = errs_[i];
            SetAddResult res;
            const bool recreated = std::find(recreated_.begin(), recreated_.end(), i) !=
                                   recreated_.end();
            if (!resent) {
                res = e1 == 0 ? SetAddResult::Added
                              : (mode == Mode::NewOnly && is_exist(e1) ? SetAddResult::Exists
                                                                       : SetAddResult::Error);
            } else if (err == 0) {
                res = e1 == 0 || recreated ? SetAddResult::Added
                                           : SetAddResult::Refreshed;
            } else {
                res = SetAddResult::Error;
            }
            if (res == SetAddResult::Error) {
                const int e = resent ? err : e1;
                last_errno_ = e == kPending ? transport_errno_ : e;
                ok = false;
            }
            out[i] = res;
        }
        return ok;
    }

    MsgBuilder b_{4096};
    bool batched_;
    std::unique_ptr<SetWriterTransport> transport_;
    int last_errno_{0};
    int transport_errno_{0};
    std::atomic<uint64_t>* slow_counter_{nullptr};
    bool logged_slow_{false};
    std::chrono::steady_clock::time_point last_slow_log_{};
    std::vector<std::size_t> all_, resend_;
    std::vector<int> errs_, first_;
    std::vector<std::size_t> refresh_;
    std::vector<std::size_t> recreated_;
    std::vector<uint8_t> refresh_seen_;
    std::vector<uint8_t> ack_seen_;
};

class IpsetWriter final : public WriterBase {
public:
    explicit IpsetWriter(std::unique_ptr<SetWriterTransport> transport)
        : WriterBase(false, std::move(transport)) {}

protected:
    void build(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) override {
        build_ipset_add(b, seq, a, exclusive);
    }
};

class NftWriter final : public WriterBase {
public:
    NftWriter(std::string table, std::unique_ptr<SetWriterTransport> transport)
        : WriterBase(true, std::move(transport)), table_(std::move(table)) {}

protected:
    void build(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) override {
        build_nft_newsetelem(b, seq, table_, a, exclusive);
    }

    bool supports_refresh() const override { return true; }

    void set_timeout_update_flag(std::shared_ptr<const std::atomic<bool>> flag) override {
        timeout_update_ = std::move(flag);
    }
    bool in_place_refresh() const override {
        return timeout_update_ != nullptr && timeout_update_->load(std::memory_order_relaxed);
    }
    void build_in_place(MsgBuilder& b, uint32_t seq, const SetAdd& a) override {
        build_nft_refresh_setelem(b, seq, table_, a);
    }

    void build_refresh(MsgBuilder& b, uint32_t del_seq, uint32_t add_seq,
                       const SetAdd& a) override {
        build_nft_delsetelem(b, del_seq, table_, a);
        build_nft_newsetelem(b, add_seq, table_, a, false);
    }

private:
    std::string table_;
    std::shared_ptr<const std::atomic<bool>> timeout_update_;
};

} // namespace

void build_ipset_add(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) {
    const uint16_t flags = static_cast<uint16_t>(
        NLM_F_REQUEST | NLM_F_ACK | (exclusive ? NLM_F_EXCL : 0));
    // Userspace ipset sends AF_INET in nfgenmsg for both families (the kernel
    // takes the family from the set), so do the same.
    b.begin(static_cast<uint16_t>((NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_ADD), flags, seq,
            NFPROTO_IPV4, 0);
    b.put_u8(IPSET_ATTR_PROTOCOL, kIpsetProtocol);
    b.put_strz(IPSET_ATTR_SETNAME, a.set_name);
    const std::size_t data = b.nest_begin(IPSET_ATTR_DATA);
    const std::size_t ip = b.nest_begin(IPSET_ATTR_IP);
    if (a.family == 6) {
        b.put(IPSET_ATTR_IPADDR_IPV6 | NLA_F_NET_BYTEORDER, a.addr.data(), 16);
    } else {
        b.put(IPSET_ATTR_IPADDR_IPV4 | NLA_F_NET_BYTEORDER, a.addr.data(), 4);
    }
    b.nest_end(ip);
    b.put_u32_be(IPSET_ATTR_TIMEOUT | NLA_F_NET_BYTEORDER, a.timeout_s);
    b.nest_end(data);
    b.end();
}

namespace {
void build_nft_newsetelem_impl(MsgBuilder& b, uint32_t seq, std::string_view table,
                               const SetAdd& a, bool exclusive, bool with_expiration) {
    const uint16_t flags = static_cast<uint16_t>(
        NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK | (exclusive ? NLM_F_EXCL : 0));
    b.begin(static_cast<uint16_t>((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWSETELEM), flags, seq,
            NFPROTO_INET, 0);
    b.put_strz(NFTA_SET_ELEM_LIST_TABLE, table);
    b.put_strz(NFTA_SET_ELEM_LIST_SET, a.set_name);
    const std::size_t elems = b.nest_begin(NFTA_SET_ELEM_LIST_ELEMENTS);
    const std::size_t elem = b.nest_begin(NFTA_LIST_ELEM);
    if (a.timeout_s != 0) {
        b.put_u64_be(NFTA_SET_ELEM_TIMEOUT, static_cast<uint64_t>(a.timeout_s) * 1000u);
        if (with_expiration) {
            b.put_u64_be(NFTA_SET_ELEM_EXPIRATION, static_cast<uint64_t>(a.timeout_s) * 1000u);
        }
    }
    const std::size_t key = b.nest_begin(NFTA_SET_ELEM_KEY);
    b.put(NFTA_DATA_VALUE, a.addr.data(), a.family == 6 ? 16 : 4);
    b.nest_end(key);
    b.nest_end(elem);
    b.nest_end(elems);
    b.end();
}
} // namespace

void build_nft_newsetelem(MsgBuilder& b, uint32_t seq, std::string_view table, const SetAdd& a,
                          bool exclusive) {
    build_nft_newsetelem_impl(b, seq, table, a, exclusive, /*with_expiration=*/false);
}

void build_nft_refresh_setelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                               const SetAdd& a) {
    build_nft_newsetelem_impl(b, seq, table, a, /*exclusive=*/false, /*with_expiration=*/true);
}

void build_nft_delsetelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                          const SetAdd& a) {
    const uint16_t flags = static_cast<uint16_t>(NLM_F_REQUEST | NLM_F_ACK);
    b.begin(static_cast<uint16_t>((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_DELSETELEM), flags, seq,
            NFPROTO_INET, 0);
    b.put_strz(NFTA_SET_ELEM_LIST_TABLE, table);
    b.put_strz(NFTA_SET_ELEM_LIST_SET, a.set_name);
    const std::size_t elems = b.nest_begin(NFTA_SET_ELEM_LIST_ELEMENTS);
    const std::size_t elem = b.nest_begin(NFTA_LIST_ELEM);
    const std::size_t key = b.nest_begin(NFTA_SET_ELEM_KEY);
    b.put(NFTA_DATA_VALUE, a.addr.data(), a.family == 6 ? 16 : 4);
    b.nest_end(key);
    b.nest_end(elem);
    b.nest_end(elems);
    b.end();
}

std::unique_ptr<SetWriterTransport> make_netlink_transport(int rcvbuf_bytes) {
    return std::make_unique<NlSocketTransport>(rcvbuf_bytes);
}

std::unique_ptr<DynamicSetWriter> make_ipset_writer() {
    return std::make_unique<IpsetWriter>(std::make_unique<NlSocketTransport>(1 << 18));
}

std::unique_ptr<DynamicSetWriter> make_nft_writer(std::string table) {
    return std::make_unique<NftWriter>(std::move(table),
                                       std::make_unique<NlSocketTransport>(1 << 18));
}

std::unique_ptr<DynamicSetWriter>
make_ipset_writer_for_test(std::unique_ptr<SetWriterTransport> transport) {
    return std::make_unique<IpsetWriter>(std::move(transport));
}

std::unique_ptr<DynamicSetWriter>
make_nft_writer_for_test(std::string table, std::unique_ptr<SetWriterTransport> transport) {
    return std::make_unique<NftWriter>(std::move(table), std::move(transport));
}

} // namespace keen_pbr3::nfnl
