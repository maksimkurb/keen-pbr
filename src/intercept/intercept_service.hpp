#pragma once

#include "intercept_processor.hpp"
#include "../netfilter/nflog.hpp"
#include "../netfilter/nfqueue.hpp"

#include <condition_variable>
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
};

class InterceptService {
public:
    // `writer` is used by the hot thread only.
    explicit InterceptService(std::unique_ptr<nfnl::DynamicSetWriter> writer);
    ~InterceptService();
    InterceptService(const InterceptService&) = delete;
    InterceptService& operator=(const InterceptService&) = delete;

    // Opens NfQueue/NfLog per options and starts the hot and cleanup threads.
    // Throws std::runtime_error (or nfnl::NlSocketError) if binding fails.
    void start(const InterceptServiceOptions& options, std::shared_ptr<const InterceptSnapshot> snapshot);
    void update_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot);
    // Hot thread drains the queue, ACCEPTs everything still held, then unbinds.
    void stop();
    bool running() const;
    const InterceptCounters& counters() const { return counters_; }
    std::vector<InterceptEvent> events_since(uint64_t after_seq, std::size_t max) const;
    uint64_t last_event_seq() const { return processor_.last_event_seq(); }

private:
    // Bounded MPSC queue feeding the cleanup thread.  request() never blocks
    // beyond a short critical section and drops (counting) when full.
    class CleanupQueue : public ConntrackCleanupSink {
    public:
        explicit CleanupQueue(InterceptCounters& counters) : counters_(counters) {}
        void request(uint8_t family, const std::array<uint8_t, 16>& dst) override;
        // Blocks until work arrives or stop; debounces, then returns the batch.
        bool wait_batch(std::vector<std::pair<uint8_t, std::array<uint8_t, 16>>>& out);
        void shutdown();

        static constexpr std::size_t kCapacity = 1024;

    private:
        InterceptCounters& counters_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<std::pair<uint8_t, std::array<uint8_t, 16>>> pending_;
        bool stopping_{false};
    };

    void hot_loop();
    void cleanup_loop();
    void handle_queue_packet(const nfnl::QueuedPacket& packet,
                             std::chrono::steady_clock::time_point deadline);
    void drain_queue();
    void run_cleanup(const std::vector<std::pair<uint8_t, std::array<uint8_t, 16>>>& batch);

    std::unique_ptr<nfnl::DynamicSetWriter> writer_;
    InterceptCounters counters_;
    CleanupQueue cleanup_queue_;
    InterceptProcessor processor_;

    InterceptServiceOptions options_;
    std::unique_ptr<nfnl::NfQueue> queue_;
    std::unique_ptr<nfnl::NfLog> log_;
    int stop_fd_{-1};
    std::thread hot_thread_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
    bool replacement_allowed_{false};
    uint32_t max_packet_id_{0};
    bool have_packet_id_{false};
};

} // namespace keen_pbr3
