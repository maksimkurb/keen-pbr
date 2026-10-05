#include "intercept_service.hpp"

#include "../log/logger.hpp"
#include "../netfilter/conntrack.hpp"

#include "../netfilter/uapi_compat.hpp"  // IWYU pragma: keep (macro compat shims)
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace keen_pbr3 {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCleanupDebounce = std::chrono::milliseconds(50);
constexpr auto kCleanupDumpBudget = std::chrono::milliseconds(2000);

} // namespace

bool InterceptService::WriteGate::enter() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused_) return false;
    ++in_flight_;
    return true;
}

void InterceptService::WriteGate::leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (in_flight_ != 0) --in_flight_;
    if (in_flight_ == 0) cv_.notify_all();
}

void InterceptService::WriteGate::pause() {
    std::unique_lock<std::mutex> lock(mutex_);
    paused_ = true;
    cv_.wait(lock, [this] { return in_flight_ == 0; });
}

void InterceptService::WriteGate::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
    }
    cv_.notify_all();
}

// ---------------------------------------------------------------- cleanup queue

void InterceptService::CleanupQueue::request(uint8_t family, const std::array<uint8_t, 16>& client,
                                             const std::array<uint8_t, 16>& dst) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (pending_.size() >= kCapacity) {
            counters_.conntrack_errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        pending_.push_back(CleanupRequest{family, client, dst});
    }
    cv_.notify_one();
}

bool InterceptService::CleanupQueue::wait_batch(std::vector<CleanupRequest>& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
    if (stopping_) return false;
    // Debounce: let a burst of requests accumulate so one dump serves every address of a client.
    cv_.wait_for(lock, kCleanupDebounce, [this] { return stopping_; });
    if (stopping_) return false;
    out.swap(pending_);
    pending_.clear();
    return true;
}

void InterceptService::CleanupQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
}

void InterceptService::CleanupQueue::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = false;
    pending_.clear();
}

// ---------------------------------------------------------------- service

InterceptService::InterceptService(std::unique_ptr<nfnl::DynamicSetWriter> writer,
                                   std::unique_ptr<nfnl::DynamicSetWriter> l7_writer)
    : writer_(std::move(writer)),
      l7_writer_(std::move(l7_writer)),
      cleanup_queue_(counters_),
      processor_(*writer_, cleanup_queue_, counters_) {
    writer_->set_slow_write_counter(&counters_.set_write_slow);
    if (l7_writer_) l7_writer_->set_slow_write_counter(&counters_.set_write_slow);
    processor_.set_writer_callbacks(
        [this] { return dns_writes_.enter(); },
        [this] { dns_writes_.leave(); },
        [this] { return l7_writes_.enter(); },
        [this] { l7_writes_.leave(); });
}

InterceptService::~InterceptService() { stop(); }

InterceptService::WritePause::~WritePause() {
    if (service_ != nullptr) {
        service_->l7_writes_.resume();
        service_->dns_writes_.resume();
    }
}

InterceptService::WritePause InterceptService::pause_writes() {
    dns_writes_.pause();
    l7_writes_.pause();
    return WritePause(*this);
}

void InterceptService::invalidate_snapshot() {
    processor_.set_snapshot(nullptr);
    snapshot_ready_.store(false, std::memory_order_release);
}

void InterceptService::discard_l7_pending() {
    std::deque<InterceptL7Work> dropped;
    {
        std::lock_guard<std::mutex> lock(l7_mutex_);
        dropped.swap(l7_pending_);
    }
    for (auto& work : dropped) processor_.reject_l7_work(std::move(work));
}

bool InterceptService::running() const { return running_.load(); }

void InterceptService::update_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot) {
    processor_.set_snapshot(std::move(snapshot));
    snapshot_ready_.store(true, std::memory_order_release);
}

std::vector<InterceptEvent> InterceptService::events_since(uint64_t after_seq,
                                                           std::size_t max) const {
    return processor_.events_since(after_seq, max);
}

void InterceptService::start(const InterceptServiceOptions& options,
                             std::shared_ptr<const InterceptSnapshot> snapshot) {
    if (running_.load()) throw std::runtime_error("intercept service already running");
    // A fatal hot-loop exit marks running=false before its worker objects are
    // joined.  Reuse must drain those stale workers before resetting the
    // queue state, otherwise a new L7 worker could run beside the old one.
    if (hot_thread_.joinable() || l7_thread_.joinable() || cleanup_thread_.joinable()) {
        stop();
    }
    if (!writer_) throw std::runtime_error("intercept service has no set writer");
    options_ = options;
    cleanup_queue_.reset();
    {
        std::lock_guard<std::mutex> lock(l7_mutex_);
        l7_stopping_ = false;
        l7_pending_.clear();
    }
    failed_.store(false, std::memory_order_release);
    l7_degraded_.store(false, std::memory_order_release);
    snapshot_ready_.store(false, std::memory_order_release);
    processor_.set_snapshot(std::move(snapshot));

    if (l7_writer_) {
        processor_.set_l7_submitter([this](InterceptL7Work work) {
            submit_l7_work(std::move(work));
        });
    } else {
        processor_.set_l7_submitter({});
    }

    cleanup_queue_.set_enabled(options.conntrack_cleanup);
    listener_probe_ = InterceptRuntimeProbe{};
    dns_bound_.store(false, std::memory_order_release);
    l7_bound_.store(false, std::memory_order_release);
    replacement_allowed_ = false;
    std::string queue_error;
    std::string log_error;
    if (options.queue_num) {
        try {
            nfnl::NfQueueOptions qopt;
            qopt.queue_num = *options.queue_num;
            qopt.fail_open = options.fail_open;
            queue_ = std::make_unique<nfnl::NfQueue>(qopt);
            replacement_allowed_ = queue_->payload_replacement_supported();
            listener_probe_.nfqueue = nfnl::make_probe_result(nfnl::ProbeStatus::ok, "queue bound");
            listener_probe_.fail_open = queue_->fail_open_probe();
            listener_probe_.replacement = queue_->payload_replacement();
            dns_bound_.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            queue_.reset();
            queue_error = e.what();
            const auto* nl = dynamic_cast<const nfnl::NlSocketError*>(&e);
            listener_probe_.nfqueue = nfnl::classify_errno(nl != nullptr ? nl->code() : EIO, "NFQUEUE bind");
            if (!listener_probe_.nfqueue.blocks()) {
                listener_probe_.nfqueue.status = nfnl::ProbeStatus::error;
            }
            listener_probe_.nfqueue.reason = queue_error;
            if (nl != nullptr) {
                const auto hint = nfnl::nfnl_module_missing_hint(true, nl->code());
                if (!hint.empty()) {
                    listener_probe_.nfqueue.reason = hint + "; " + queue_error;
                    queue_error = listener_probe_.nfqueue.reason;
                }
            }
        }
    }
    if (options.nflog_group) {
        try {
            nfnl::NfLogOptions lopt;
            lopt.group = *options.nflog_group;
            log_ = std::make_unique<nfnl::NfLog>(lopt);
            listener_probe_.nflog = nfnl::make_probe_result(nfnl::ProbeStatus::ok, "group bound");
            l7_bound_.store(true, std::memory_order_release);
        } catch (const std::exception& e) {
            log_.reset();
            log_error = e.what();
            const auto* nl = dynamic_cast<const nfnl::NlSocketError*>(&e);
            listener_probe_.nflog = nfnl::classify_errno(nl != nullptr ? nl->code() : EIO, "NFLOG bind");
            if (!listener_probe_.nflog.blocks()) {
                listener_probe_.nflog.status = nfnl::ProbeStatus::error;
            }
            listener_probe_.nflog.reason = log_error;
            if (nl != nullptr) {
                const auto hint = nfnl::nfnl_module_missing_hint(false, nl->code());
                if (!hint.empty()) {
                    listener_probe_.nflog.reason = hint + "; " + log_error;
                    log_error = listener_probe_.nflog.reason;
                }
            }
        }
    }
    if ((options.queue_num || options.nflog_group) && !queue_ && !log_) {
        std::string detail = queue_error;
        if (!log_error.empty()) detail += (detail.empty() ? "" : "; ") + log_error;
        throw std::runtime_error(std::string("intercept: cannot bind netfilter queue/log (queue=") +
                                 (options.queue_num ? std::to_string(*options.queue_num) : "-") +
                                 " nflog=" +
                                 (options.nflog_group ? std::to_string(*options.nflog_group) : "-") +
                                 "): " + detail);
    }

    stop_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stop_fd_ < 0) {
        const int err = errno;
        queue_.reset();
        log_.reset();
        throw std::runtime_error(std::string("intercept: eventfd failed: ") + std::strerror(err));
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        const int err = errno;
        ::close(stop_fd_);
        stop_fd_ = -1;
        queue_.reset();
        log_.reset();
        throw std::runtime_error(std::string("intercept: epoll_create1 failed: ") +
                                 std::strerror(err));
    }
    const auto add_fd = [this](int fd) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            const int err = errno;
            throw std::runtime_error(std::string("intercept: epoll_ctl failed: ") +
                                     std::strerror(err));
        }
    };
    try {
        add_fd(stop_fd_);
        if (queue_) add_fd(queue_->fd());
        if (log_) add_fd(log_->fd());
    } catch (...) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
        ::close(stop_fd_);
        stop_fd_ = -1;
        queue_.reset();
        log_.reset();
        throw;
    }

    have_packet_id_ = false;
    running_.store(true);
    try {
        cleanup_thread_ = std::thread([this] { cleanup_loop(); });
        if (l7_writer_) l7_thread_ = std::thread([this] { l7_loop(); });
        hot_thread_ = std::thread([this] { hot_loop(); });
    } catch (...) {
        running_.store(false);
        cleanup_queue_.shutdown();
        stop_l7_worker();
        if (cleanup_thread_.joinable()) cleanup_thread_.join();
        if (l7_thread_.joinable()) l7_thread_.join();
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
        ::close(stop_fd_);
        stop_fd_ = -1;
        queue_.reset();
        log_.reset();
        throw;
    }
}

void InterceptService::stop() {
    if (!hot_thread_.joinable() && !cleanup_thread_.joinable() && !l7_thread_.joinable()) return;
    if (stop_fd_ >= 0) {
        const uint64_t one = 1;
        const ssize_t rc = ::write(stop_fd_, &one, sizeof(one));
        (void)rc;
    }
    if (hot_thread_.joinable()) hot_thread_.join();  // drains + ACCEPTs, then returns
    stop_l7_worker();
    if (l7_thread_.joinable()) l7_thread_.join();
    cleanup_queue_.shutdown();
    if (cleanup_thread_.joinable()) cleanup_thread_.join();
    queue_.reset();  // unbinds
    log_.reset();
    if (stop_fd_ >= 0) {
        ::close(stop_fd_);
        stop_fd_ = -1;
    }
    running_.store(false);
}

void InterceptService::handle_queue_packet(const nfnl::QueuedPacket& packet,
                                           const DnsRound& round) {
    if (!have_packet_id_ || static_cast<int32_t>(packet.packet_id - max_packet_id_) > 0) {
        max_packet_id_ = packet.packet_id;
        have_packet_id_ = true;
    }
    InterceptProcessor::DnsDecision decision;
    try {
        // A GSO super-packet is never rewritten: its payload spans several wire segments.
        decision = processor_.process_dns_packet(packet.payload, round,
                                                 replacement_allowed_ && !packet.gso);
    } catch (...) {
        decision = {};
    }
    const bool ok = (decision.replace && decision.replacement != nullptr)
                        ? queue_->verdict(packet.packet_id, NF_ACCEPT, decision.replacement->data(),
                                          decision.replacement->size())
                        : queue_->verdict(packet.packet_id, NF_ACCEPT);
    if (!ok) {
        Logger::instance().warn("intercept: NFQUEUE verdict send failed (errno={})", queue_->last_errno());
    }
    // Verdict first: recording the observation never adds to the hold latency.
    processor_.commit_dns_event();
}

void InterceptService::drain_queue() {
    // Accept everything still readable, then batch-accept what we know about.
    // Closing the queue would otherwise DROP packets still queued in the kernel.
    int idle_rounds = 0;
    for (int i = 0; i < 200 && idle_rounds < 2; ++i) {
        const int n = queue_->receive([this](const nfnl::QueuedPacket& packet) {
            if (!have_packet_id_ || static_cast<int32_t>(packet.packet_id - max_packet_id_) > 0) {
                max_packet_id_ = packet.packet_id;
                have_packet_id_ = true;
            }
            queue_->verdict(packet.packet_id, NF_ACCEPT);
        });
        if (n < 0) break;
        if (n == 0) {
            ++idle_rounds;
            ::poll(nullptr, 0, 5);
        } else {
            idle_rounds = 0;
        }
    }
    if (have_packet_id_) queue_->verdict_batch_accept(max_packet_id_);
}

void InterceptService::hot_loop() {
    const int ep = epoll_fd_;

    const auto hold = std::chrono::milliseconds(std::max(1, options_.hold_timeout_ms));
    bool failed = false;
    while (!failed) {
        epoll_event events[4];
        const int n = ::epoll_wait(ep, events, 4, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            Logger::instance().error("intercept: epoll_wait failed: {}", std::strerror(errno));
            failed = true;
            break;
        }
        // Budget starts when the kernel handed us the wakeup; shared by all
        // packets read in this round.
        const Clock::time_point woke = Clock::now();
        const Clock::time_point deadline = woke + hold;
        bool stop = false;
        bool queue_ready = false;
        bool log_ready = false;
        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            if (fd == stop_fd_) stop = true;
            else if (queue_ && fd == queue_->fd()) queue_ready = true;
            else if (log_ && fd == log_->fd()) log_ready = true;
        }
        if (queue_ready) {
            const uint64_t round_first_seq = processor_.last_event_seq() + 1;
            uint32_t batch_pos = 0;
            const int rc = queue_->receive([this, woke, deadline, &batch_pos](const nfnl::QueuedPacket& packet) {
                handle_queue_packet(packet, DnsRound(woke, deadline, batch_pos++));
            });
            counters_.queue_overruns.store(queue_->overruns(), std::memory_order_relaxed);
            // Every verdict of this round is out: now write, once, whatever
            // missed the hold deadline (bounded combined budget, with backoff).
            processor_.flush_late_writes();
            if (batch_pos > 0) processor_.set_round_batch_size(round_first_seq, batch_pos);
            if (rc < 0) {
                Logger::instance().error("intercept: NFQUEUE receive failed (errno={}); stopping hot thread",
                                         queue_->last_errno());
                failed = true;
            }
        }
        if (log_ready && !failed) {
            const int rc = log_->receive([this](const nfnl::LoggedPacket& packet) {
                processor_.on_l7_packet(packet.payload, Clock::now());
            });
            counters_.log_overruns.store(log_->overruns(), std::memory_order_relaxed);
            if (rc < 0) {
                Logger::instance().error("intercept: NFLOG receive failed (errno={})", log_->last_errno());
                l7_degraded_.store(true, std::memory_order_release);
                log_.reset();  // keep serving the DNS queue
                // fd removed from epoll automatically on close
            }
        }
        if (stop) break;
    }
    if (queue_) drain_queue();
    if (failed) {
        failed_.store(true, std::memory_order_release);
        stop_l7_worker();
        queue_.reset();
        log_.reset();
    }
    running_.store(false, std::memory_order_release);
    if (ep >= 0) {
        ::close(ep);
        epoll_fd_ = -1;
    }
}

void InterceptService::submit_l7_work(InterceptL7Work work) {
    bool rejected = false;
    {
        std::lock_guard<std::mutex> lock(l7_mutex_);
        if (l7_stopping_ || l7_pending_.size() >= kL7Capacity) {
            rejected = true;
        } else {
            l7_pending_.push_back(std::move(work));
        }
    }
    if (rejected) {
        processor_.reject_l7_work(std::move(work));
        return;
    }
    l7_cv_.notify_one();
}

void InterceptService::stop_l7_worker() {
    std::deque<InterceptL7Work> dropped;
    {
        std::lock_guard<std::mutex> lock(l7_mutex_);
        l7_stopping_ = true;
        dropped.swap(l7_pending_);
    }
    l7_cv_.notify_all();
    for (auto& work : dropped) processor_.reject_l7_work(std::move(work));
}

void InterceptService::l7_loop() {
    for (;;) {
        InterceptL7Work work;
        {
            std::unique_lock<std::mutex> lock(l7_mutex_);
            l7_cv_.wait(lock, [this] { return l7_stopping_ || !l7_pending_.empty(); });
            if (l7_pending_.empty()) {
                if (l7_stopping_) return;
                continue;
            }
            work = std::move(l7_pending_.front());
            l7_pending_.pop_front();
        }
        processor_.process_l7_work(std::move(work), *l7_writer_);
    }
}

void InterceptService::cleanup_loop() {
    std::vector<CleanupRequest> batch;
    while (cleanup_queue_.wait_batch(batch)) {
        try {
            run_cleanup(batch);
        } catch (const std::exception& e) {
            counters_.conntrack_errors.fetch_add(1, std::memory_order_relaxed);
            Logger::instance().debug("intercept: conntrack cleanup failed: {}", e.what());
        }
        batch.clear();
    }
}

void InterceptService::run_cleanup(const std::vector<CleanupRequest>& batch) {
    // One dump per (family, client): a DNS answer with several addresses, or a
    // burst of answers for the same client, is a single cleanup operation.
    using ClientKey = std::pair<uint8_t, std::array<uint8_t, 16>>;
    std::map<ClientKey, std::set<std::array<uint8_t, 16>>> groups;
    for (const CleanupRequest& item : batch) {
        const std::size_t len = item.family == 6 ? 16 : 4;
        ClientKey key{item.family, {}};
        std::memcpy(key.second.data(), item.client.data(), len);
        std::array<uint8_t, 16> dst{};
        std::memcpy(dst.data(), item.dst.data(), len);
        groups[key].insert(dst);
    }
    for (auto& [key, dsts] : groups) {
        counters_.conntrack_requests.fetch_add(dsts.size(), std::memory_order_relaxed);
        cleanup_client(key.first, key.second, std::vector<std::array<uint8_t, 16>>(dsts.begin(), dsts.end()));
    }
}

void InterceptService::cleanup_client(uint8_t family, const std::array<uint8_t, 16>& client,
                                      std::vector<std::array<uint8_t, 16>> dsts) {
    const nfnl::ConntrackFamily ct_family =
        family == 6 ? nfnl::ConntrackFamily::ipv6 : nfnl::ConntrackFamily::ipv4;
    nfnl::ConntrackOptions options;
    // Scope: original tuple client -> learned destination, whatever the kernel
    // pre-filter did.  Other clients and other destinations are never touched.
    options.filter = nfnl::make_client_destination_filter(ct_family, client, std::move(dsts));

    // Dump only the client's flows when the kernel can filter by source;
    // otherwise the whole table is dumped and filtered in userspace.
    bool use_kernel_filter = !ct_kernel_filter_unsupported_.load(std::memory_order_relaxed);
    std::vector<nfnl::ConntrackEntry> entries;
    for (;;) {
        options.kernel_filter.reset();
        if (use_kernel_filter) options.kernel_filter = nfnl::ConntrackKernelFilter{client};
        nfnl::ConntrackDump dump(ct_family, options);
        const Clock::time_point give_up = Clock::now() + kCleanupDumpBudget;
        int rc = 0;
        while (rc == 0 && Clock::now() < give_up) rc = dump.receive(100);
        if (use_kernel_filter &&
            ((rc < 0 && nfnl::conntrack_kernel_filter_refused(dump.last_errno())) ||
             (dump.complete() && dump.kernel_filter_mismatches() != 0))) {
            // Refused (strict old kernel) or ignored (the kernel sent entries of
            // other clients): remember it and redo this cleanup unfiltered.
            ct_kernel_filter_unsupported_.store(true, std::memory_order_relaxed);
            Logger::instance().debug(
                "intercept: ctnetlink dump pre-filter not supported, using a full dump");
            use_kernel_filter = false;
            continue;
        }
        if (rc < 0 || !dump.complete()) {
            counters_.conntrack_errors.fetch_add(1, std::memory_order_relaxed);
        }
        entries = dump.entries();
        break;
    }
    for (std::size_t off = 0; off < entries.size(); off += options.max_delete_batch) {
        const std::size_t end = std::min(entries.size(), off + options.max_delete_batch);
        std::vector<nfnl::ConntrackEntry> chunk(entries.begin() + static_cast<std::ptrdiff_t>(off),
                                                entries.begin() + static_cast<std::ptrdiff_t>(end));
        nfnl::ConntrackDeleteBatch del(chunk, options);
        const Clock::time_point del_give_up = Clock::now() + kCleanupDumpBudget;
        std::size_t last_pending = del.pending() + 1;
        while (!del.complete() && Clock::now() < del_give_up) {
            const int r = del.receive(100);
            if (r < 0 && del.pending() == last_pending) break;  // no progress on error
            last_pending = del.pending();
        }
        counters_.conntrack_deleted.fetch_add(del.succeeded(), std::memory_order_relaxed);
        counters_.conntrack_errors.fetch_add(del.failures(), std::memory_order_relaxed);
    }
}

} // namespace keen_pbr3
