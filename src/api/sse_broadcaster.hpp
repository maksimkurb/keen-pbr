#pragma once

#ifdef WITH_API

#include "../util/traced_mutex.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

class SseBroadcaster {
public:
    struct Subscription {
        TracedMutex mutex;
        std::condition_variable_any cv;
        std::deque<std::string> messages GUARDED_BY(mutex);
        bool closed GUARDED_BY(mutex){false};
        // Lossy mode only: messages dropped because the queue was full.
        size_t dropped GUARDED_BY(mutex){0};
        std::string first_dropped GUARDED_BY(mutex);
        std::string last_dropped GUARDED_BY(mutex);
        std::function<bool(const std::string&)> filter;
        bool close_after_filtered_message{false};
    };

    using SubscriptionPtr = std::shared_ptr<Subscription>;

    // Builds the notice delivered in place of dropped messages (given the first
    // and last dropped message); an empty result means "no notice".
    using GapBuilder = std::function<std::string(const std::string& first, const std::string& last)>;
    using MessageFilter = std::function<bool(const std::string&)>;

    // Without `gap_builder` a subscriber whose queue is full is closed.  With
    // it, messages are dropped instead and the next delivery is preceded by
    // the notice, so the subscriber learns exactly that something was lost.
    explicit SseBroadcaster(size_t max_queue_size = 128, GapBuilder gap_builder = nullptr);

    SubscriptionPtr subscribe();
    SubscriptionPtr subscribe(std::vector<std::string> initial_messages,
                              MessageFilter filter = {},
                              bool close_after_filtered_message = false);
    void unsubscribe(const SubscriptionPtr& subscription);
    bool has_subscribers();
    void publish(const std::string& message);
    void close_all();

private:
    void compact_locked() REQUIRES(mutex_);

    size_t max_queue_size_;
    GapBuilder gap_builder_;
    TracedMutex mutex_;
    std::vector<std::weak_ptr<Subscription>> subscriptions_ GUARDED_BY(mutex_);
};

} // namespace keen_pbr3

#endif // WITH_API
