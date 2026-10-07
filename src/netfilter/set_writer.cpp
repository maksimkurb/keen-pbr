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

// Shared write logic; backends supply message construction and the way an
// existing element's timeout is extended.  The backend (and, for nft, whether
// the kernel can extend a timeout in place) is fixed at construction from the
// startup capability snapshot; nothing is probed per write.
//
// Every call writes each chunk once.  Elements that failed with a transient
// error (see is_transient()) are then retried ONCE as a smaller write of the
// same kind, bounded by the caller's deadline; there is no further ladder.
//
// add() is an upsert: where the backend can create-or-extend with one
// non-exclusive request per element (ipset, nft with in-place support) that is
// the whole write on the hot path, and every success is Added.  Otherwise it
// falls back to an exclusive add plus a refresh of what already existed.
class WriterBase : public DynamicSetWriter {
public:
    WriterBase(bool batched, std::unique_ptr<SetWriterTransport> transport)
        : batched_(batched), transport_(std::move(transport)) {}

    int last_errno() const override { return last_errno_; }

    void set_slow_write_counter(std::atomic<uint64_t>* counter) override { slow_counter_ = counter; }
    void set_metrics(Metrics* metrics) override { metrics_ = metrics; }

    bool add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::Upsert, adds, out, count, timeout_ms);
    }
    bool add_new(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::NewOnly, adds, out, count, timeout_ms);
    }
    bool refresh(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        return timed_add(Mode::Existing, adds, out, count, timeout_ms);
    }

protected:
    // How refresh extends an existing element's timeout.
    enum class RefreshKind : uint8_t {
        NonExclusiveAdd,  // one non-exclusive add per element (ipset)
        InPlace,          // one non-exclusive NEWSETELEM with expiration (nft, kernel probed ok)
        DeleteAdd,        // delete + add per element (nft without in-place support)
    };

    virtual void build(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) = 0;
    virtual RefreshKind refresh_kind() const { return RefreshKind::NonExclusiveAdd; }
    virtual void build_refresh(MsgBuilder&, uint32_t, uint32_t, const SetAdd&) {}
    virtual void build_in_place(MsgBuilder&, uint32_t, const SetAdd&) {}

private:
    // Upsert: one non-exclusive add per element when refresh_kind() allows it
    //   (see write_once()), otherwise as Full (add()).
    // Full: exclusive add, then refresh of what existed; also the retry of
    //   Existing.
    // NewOnly: exclusive add only; existing elements -> Exists (add_new()).
    // Existing: skip the exclusive add, refresh every element (refresh()).
    enum class Mode : uint8_t { Upsert, Full, NewOnly, Existing };

    // A retryable failure: the request may simply not have been processed
    // (EAGAIN/EINTR/ENOBUFS), or - for the nft delete+add refresh only - the
    // element expired between the caller's belief and the DELSETELEM (ENOENT).
    bool is_transient(int err) const {
        return err == EAGAIN || err == EINTR || err == ENOBUFS ||
               (err == ENOENT && delete_add_ran_);
    }

    bool timed_add(Mode mode, const SetAdd* adds, SetAddResult* out, std::size_t count,
                   int timeout_ms) {
        if (count == 0) return true;
        (void)transport_->take_send_us();
        const auto started = std::chrono::steady_clock::now();
        const bool ok = add_impl(mode, adds, out, count, timeout_ms);
        const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
        const auto send_us = transport_->take_send_us();
        if (metrics_ != nullptr) {
            const auto total = static_cast<uint64_t>(std::max<long long>(total_us, 0));
            metrics_->total.record(total);
            metrics_->send.record(send_us);
            metrics_->remainder.record(total > send_us ? total - send_us : 0);
        }
        if (total_us >= static_cast<long long>(kSlowWriteMs) * 1000) {
            note_slow_write(static_cast<uint64_t>(total_us), send_us, count,
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

    // nft delete+add refresh of sel[0..m): one batch of DELSETELEM+NEWSETELEM
    // pairs.  A batch-level error rolls back every element.
    int run_delete_add(const SetAdd* adds, const std::size_t* sel, std::size_t m,
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

    // Extends the timeout of the elements adds[sel[0..m)] that are believed to
    // exist, storing 0 or an errno in errs[sel[i]].
    void refresh_subset(const SetAdd* adds, const std::size_t* sel, std::size_t m, int* errs,
                        std::chrono::steady_clock::time_point deadline) {
        for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = kPending;
        RefreshKind kind = refresh_kind();
        // In-place extension needs a timeout to restart; permanent elements
        // (and so any batch containing one) take the delete+add path.
        if (kind == RefreshKind::InPlace &&
            std::any_of(sel, sel + m, [&](std::size_t i) { return adds[i].timeout_s == 0; })) {
            kind = RefreshKind::DeleteAdd;
        }
        if (kind == RefreshKind::DeleteAdd) {
            delete_add_ran_ = true;
            (void)run_delete_add(adds, sel, m, errs, deadline);
        } else {
            // InPlace: one transaction, so an error rolls back every element.
            // NonExclusiveAdd (ipset): one request per element, own ack each.
            const int rc = run_pass(adds, sel, m, /*exclusive=*/false, errs, deadline,
                                    /*in_place=*/kind == RefreshKind::InPlace);
            if (rc != 0) {
                for (std::size_t i = 0; i < m; ++i) errs[sel[i]] = rc;
            }
        }
        for (std::size_t i = 0; i < m; ++i) {
            if (errs[sel[i]] == kPending) errs[sel[i]] = transport_errno_;
        }
    }

    // An nft batch is one transaction: any error aborts it and rolls back its
    // other elements even though they were acked, so after a failed first pass
    // every acknowledged element is resent non-exclusively (as the same kind of
    // message as the first pass).  ipset requests are independent and never
    // need this.
    void resend_acked(const SetAdd* adds, std::size_t n, int first_rc, bool in_place,
                      std::chrono::steady_clock::time_point deadline) {
        bool any_error = first_rc != 0;
        for (std::size_t i = 0; i < n; ++i) any_error = any_error || errs_[i] != 0;
        resend_.clear();
        if (batched_ && any_error) {
            for (std::size_t i = 0; i < n; ++i) {
                if (errs_[i] == 0 && ack_seen_[i]) resend_.push_back(i);
            }
        }
        if (resend_.empty()) return;
        for (const std::size_t i : resend_) errs_[i] = kPending;
        const int rc = run_pass(adds, resend_.data(), resend_.size(), /*exclusive=*/false,
                                errs_.data(), deadline, in_place);
        // A transport/batch error means no resend result is publishable.
        if (rc != 0) {
            for (const std::size_t i : resend_) errs_[i] = rc;
        }
        for (const std::size_t i : resend_) {
            if (errs_[i] == kPending) errs_[i] = transport_errno_;
        }
    }

    // Writes one chunk once, without retry.  out[i] is the result and fin[i]
    // the errno behind an Error (0 otherwise).
    void write_once(Mode mode, const SetAdd* adds, SetAddResult* out, int* fin, std::size_t n,
                    std::chrono::steady_clock::time_point deadline) {
        all_.resize(n);
        for (std::size_t i = 0; i < n; ++i) all_[i] = i;
        errs_.assign(n, kPending);
        transport_errno_ = ETIMEDOUT;
        refreshed_.assign(n, 0);

        if (mode == Mode::Existing) {
            refresh_subset(adds, all_.data(), n, errs_.data(), deadline);
            refreshed_.assign(n, 1);
        } else {
            // Upsert in a single non-exclusive pass: ipset always, nft when the
            // kernel extends timeouts in place and no element is permanent (the
            // same whole-chunk rule as refresh_subset()).  The result cannot
            // tell a created element from a refreshed one, so all are Added.
            const RefreshKind kind = refresh_kind();
            const bool upsert =
                mode == Mode::Upsert &&
                (kind == RefreshKind::NonExclusiveAdd ||
                 (kind == RefreshKind::InPlace &&
                  std::none_of(adds, adds + n,
                               [](const SetAdd& a) { return a.timeout_s == 0; })));
            const bool in_place = upsert && kind == RefreshKind::InPlace;
            const int first_rc = run_pass(adds, all_.data(), n, /*exclusive=*/!upsert,
                                          errs_.data(), deadline, in_place);
            first_.assign(errs_.begin(), errs_.end());
            resend_acked(adds, n, first_rc, in_place, deadline);
            // Without the single pass, Full/Upsert refresh what already
            // existed; NewOnly reports Exists.
            if (!upsert && mode != Mode::NewOnly) {
                refresh_.clear();
                for (std::size_t i = 0; i < n; ++i) {
                    if (is_exist(first_[i])) refresh_.push_back(i);
                }
                if (!refresh_.empty()) {
                    refresh_subset(adds, refresh_.data(), refresh_.size(), errs_.data(), deadline);
                    for (const std::size_t i : refresh_) refreshed_[i] = 1;
                }
            }
        }

        for (std::size_t i = 0; i < n; ++i) {
            if (mode == Mode::NewOnly && is_exist(errs_[i])) {
                out[i] = SetAddResult::Exists;
                fin[i] = 0;
            } else if (errs_[i] == 0) {
                out[i] = refreshed_[i] ? SetAddResult::Refreshed : SetAddResult::Added;
                fin[i] = 0;
            } else {
                out[i] = SetAddResult::Error;
                fin[i] = errs_[i] == kPending ? transport_errno_ : errs_[i];
            }
        }
    }

    bool add_chunk(Mode mode, const SetAdd* adds, SetAddResult* out, std::size_t n,
                   std::chrono::steady_clock::time_point deadline) {
        delete_add_ran_ = false;
        fin_.assign(n, 0);
        write_once(mode, adds, out, fin_.data(), n, deadline);

        // The single retry: transiently failed elements only, as the same kind
        // of write.  Upserts and exclusive adds are safe to repeat (an element
        // that did land is refreshed or comes back EEXIST), so a refresh() retry
        // runs as Full: its exclusive add recreates an element that expired
        // meanwhile (reported Added).
        retry_idx_.clear();
        for (std::size_t i = 0; i < n; ++i) {
            if (out[i] == SetAddResult::Error && is_transient(fin_[i])) retry_idx_.push_back(i);
        }
        if (!retry_idx_.empty() && remaining_ms(deadline) != 0) {
            retry_adds_.clear();
            for (const std::size_t i : retry_idx_) retry_adds_.push_back(adds[i]);
            const std::size_t k = retry_idx_.size();
            retry_out_.assign(k, SetAddResult::Error);
            retry_fin_.assign(k, 0);
            delete_add_ran_ = false;
            write_once(mode == Mode::Existing ? Mode::Full : mode, retry_adds_.data(),
                       retry_out_.data(), retry_fin_.data(), k, deadline);
            for (std::size_t j = 0; j < k; ++j) {
                out[retry_idx_[j]] = retry_out_[j];
                fin_[retry_idx_[j]] = retry_fin_[j];
            }
        }

        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (out[i] != SetAddResult::Error) continue;
            last_errno_ = fin_[i];
            ok = false;
        }
        return ok;
    }

    MsgBuilder b_{4096};
    bool batched_;
    std::unique_ptr<SetWriterTransport> transport_;
    int last_errno_{0};
    int transport_errno_{0};
    bool delete_add_ran_{false};
    std::atomic<uint64_t>* slow_counter_{nullptr};
    Metrics* metrics_{nullptr};
    bool logged_slow_{false};
    std::chrono::steady_clock::time_point last_slow_log_{};
    std::vector<std::size_t> all_, resend_, refresh_, retry_idx_;
    std::vector<int> errs_, first_, fin_, retry_fin_;
    std::vector<uint8_t> refreshed_;
    std::vector<SetAdd> retry_adds_;
    std::vector<SetAddResult> retry_out_;
    std::vector<uint8_t> refresh_seen_;
    std::vector<uint8_t> ack_seen_;
};

// ipset add() and refresh() need no special message: a non-exclusive
// IPSET_CMD_ADD (no NLM_F_EXCL) is what userspace calls `ipset add -exist`.
// The kernel maps the missing NLM_F_EXCL to IPSET_FLAG_EXIST (ip_set_core.c
// flag_exist()), and hash mtype_add() then overwrites the extensions of an
// existing element, including ip_set_timeout_set(), or creates a missing one -
// all in one request.  IPSET_ATTR_CADT_FLAGS is NOT involved (it carries the
// type flags such as nomatch/before).  WriterBase therefore sends exactly one
// non-exclusive request per element in one transact and never probes first.
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
    NftWriter(std::string table, bool in_place_refresh,
              std::unique_ptr<SetWriterTransport> transport)
        : WriterBase(true, std::move(transport)), table_(std::move(table)),
          in_place_refresh_(in_place_refresh) {}

protected:
    void build(MsgBuilder& b, uint32_t seq, const SetAdd& a, bool exclusive) override {
        build_nft_newsetelem(b, seq, table_, a, exclusive);
    }

    RefreshKind refresh_kind() const override {
        return in_place_refresh_ ? RefreshKind::InPlace : RefreshKind::DeleteAdd;
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
    bool in_place_refresh_;
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

std::unique_ptr<DynamicSetWriter> make_nft_writer(std::string table, bool in_place_refresh) {
    return std::make_unique<NftWriter>(std::move(table), in_place_refresh,
                                       std::make_unique<NlSocketTransport>(1 << 18));
}

std::unique_ptr<DynamicSetWriter>
make_ipset_writer_for_test(std::unique_ptr<SetWriterTransport> transport) {
    return std::make_unique<IpsetWriter>(std::move(transport));
}

std::unique_ptr<DynamicSetWriter>
make_nft_writer_for_test(std::string table, std::unique_ptr<SetWriterTransport> transport,
                         bool in_place_refresh) {
    return std::make_unique<NftWriter>(std::move(table), in_place_refresh, std::move(transport));
}

} // namespace keen_pbr3::nfnl
