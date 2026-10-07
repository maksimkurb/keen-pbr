#pragma once

#include "intercept_capabilities.hpp"
#include "intercept_processor.hpp"
#include "../netfilter/nflog.hpp"
#include "../netfilter/nfqueue.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace keen_pbr3 {

struct InterceptServiceOptions {
    std::optional<uint16_t> queue_num;
    std::optional<uint16_t> nflog_group;
    int hold_timeout_ms{30};
    bool nft_backend{false};
    // Delete conntrack entries of re-resolved destinations (needs ctnetlink).
    bool conntrack_cleanup{true};
    // Request NFQA_CFG_F_FAIL_OPEN on the DNS queue (kept running without it
    // when the kernel does not accept the flag).
    bool fail_open{true};
};

class InterceptService {
public:
    class WritePause {
    public:
        WritePause(const WritePause&) = delete;
        WritePause& operator=(const WritePause&) = delete;
        WritePause(WritePause&& other) noexcept : service_(other.service_) {
            other.service_ = nullptr;
        }
        WritePause& operator=(WritePause&&) = delete;
        ~WritePause();

    private:
        explicit WritePause(InterceptService& service) : service_(&service) {}
        InterceptService* service_;
        friend class InterceptService;
    };

    // `writer` is used by the hot thread only.
    explicit InterceptService(std::unique_ptr<nfnl::DynamicSetWriter> writer,
                              std::unique_ptr<nfnl::DynamicSetWriter> l7_writer = nullptr,
                              std::shared_ptr<InterceptCounters> counters = nullptr);
    ~InterceptService();
    InterceptService(const InterceptService&) = delete;
    InterceptService& operator=(const InterceptService&) = delete;

    // Opens NfQueue/NfLog per options and starts the hot and cleanup threads.
    // Each requested listener is bound independently: when only one fails the
    // service runs with the other (check dns_bound()/l7_bound() and
    // listener_probe()).  Throws std::runtime_error only when listeners were
    // requested and none could be bound.
    void start(const InterceptServiceOptions& options, std::shared_ptr<const InterceptSnapshot> snapshot);
    void update_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot);
    // The snapshot the processor currently uses (null after invalidate_snapshot()).
    std::shared_ptr<const InterceptSnapshot> current_snapshot() const {
        return processor_.current_snapshot();
    }
    // Hot thread drains the queue, ACCEPTs everything still held, then unbinds.
    void stop();
    bool running() const;
    bool failed() const { return failed_.load(std::memory_order_acquire); }
    bool l7_degraded() const { return l7_degraded_.load(std::memory_order_acquire); }
    // Listener state: bound at start and still serving.
    bool dns_bound() const {
        return dns_bound_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire) &&
               !failed_.load(std::memory_order_acquire);
    }
    bool l7_bound() const {
        return l7_bound_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire) &&
               !l7_degraded_.load(std::memory_order_acquire);
    }
    // Outcome of the last start(): nfqueue / fail_open / replacement / nflog.
    // Valid after start() returned or threw.
    const InterceptRuntimeProbe& listener_probe() const { return listener_probe_; }
    bool snapshot_ready() const { return snapshot_ready_.load(std::memory_order_acquire); }
    WritePause pause_writes();
    void invalidate_snapshot();
    // Forgets which set elements were written (the sets were recreated or
    // flushed).  Safe from any thread; called after every firewall apply.
    void invalidate_set_cache() { processor_.invalidate_set_cache(); }
    void discard_l7_pending();
    const InterceptCounters& counters() const { return *counters_; }
    std::vector<InterceptEvent> events_since(uint64_t after_seq, std::size_t max) const;
    uint64_t last_event_seq() const { return processor_.last_event_seq(); }
    // Logs the DNS hold timeouts not logged yet; called by the event pump only.
    void log_hold_timeouts() { processor_.log_hold_timeouts(); }

private:
    class WriteGate {
    public:
        bool enter();
        void leave();
        void pause();
        void resume();

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool paused_{false};
        std::size_t in_flight_{0};
    };

    // One client -> destination cleanup request.
    struct CleanupRequest {
        uint8_t family{0};
        std::array<uint8_t, 16> client{};
        std::array<uint8_t, 16> dst{};
    };

    // Bounded MPSC queue feeding the cleanup thread.  request() never blocks
    // beyond a short critical section and drops (counting) when full.
    class CleanupQueue : public ConntrackCleanupSink {
    public:
        explicit CleanupQueue(InterceptCounters& counters) : counters_(counters) {}
        void request(uint8_t family, const std::array<uint8_t, 16>& client,
                     const std::array<uint8_t, 16>& dst) override;
        // Blocks until work arrives or stop; debounces, then returns the batch.
        bool wait_batch(std::vector<CleanupRequest>& out);
        void shutdown();
        void reset();
        void set_enabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }

        static constexpr std::size_t kCapacity = 1024;

    private:
        InterceptCounters& counters_;
        std::atomic<bool> enabled_{true};
        std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<CleanupRequest> pending_;
        bool stopping_{false};
    };

    void hot_loop();
    void l7_loop();
    void cleanup_loop();
    void handle_queue_packet(const nfnl::QueuedPacket& packet,
                             const DnsRound& round);
    void drain_queue();
    void run_cleanup(const std::vector<CleanupRequest>& batch);
    // Deletes the entries client -> one of `dsts`; one dump serves them all.
    void cleanup_client(uint8_t family, const std::array<uint8_t, 16>& client,
                        std::vector<std::array<uint8_t, 16>> dsts);
    void submit_l7_work(InterceptL7Work work);
    void stop_l7_worker();
    void update_queue_overruns();
    void update_log_overruns();

    std::unique_ptr<nfnl::DynamicSetWriter> writer_;
    std::unique_ptr<nfnl::DynamicSetWriter> l7_writer_;
    WriteGate dns_writes_;
    WriteGate l7_writes_;
    std::shared_ptr<InterceptCounters> counters_;
    CleanupQueue cleanup_queue_;
    InterceptProcessor processor_;

    InterceptServiceOptions options_;
    std::unique_ptr<nfnl::NfQueue> queue_;
    std::unique_ptr<nfnl::NfLog> log_;
    std::mutex l7_mutex_;
    std::condition_variable l7_cv_;
    std::deque<InterceptL7Work> l7_pending_;
    bool l7_stopping_{false};
    static constexpr std::size_t kL7Capacity = 256;
    int stop_fd_{-1};
    int epoll_fd_{-1};
    std::thread hot_thread_;
    std::thread l7_thread_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> l7_degraded_{false};
    std::atomic<bool> snapshot_ready_{false};
    std::atomic<bool> dns_bound_{false};
    std::atomic<bool> l7_bound_{false};
    uint64_t queue_overruns_seen_{0};
    uint64_t log_overruns_seen_{0};
    // Whether the kernel refuses/ignores the ctnetlink dump pre-filter is a
    // process-wide kernel fact (nfnl::conntrack_kernel_filter_unsupported()).
    InterceptRuntimeProbe listener_probe_;
    bool replacement_allowed_{false};
    uint32_t max_packet_id_{0};
    bool have_packet_id_{false};
};

} // namespace keen_pbr3
