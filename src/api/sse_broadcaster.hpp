#pragma once

#ifdef WITH_API

#include "../util/traced_mutex.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

class SseBroadcaster {
public:
    struct Subscription {
        TracedMutex mutex;
        std::condition_variable_any cv;
        std::deque<std::string> messages;
        bool closed{false};
    };

    using SubscriptionPtr = std::shared_ptr<Subscription>;

    explicit SseBroadcaster(size_t max_queue_size = 128);

    SubscriptionPtr subscribe();
    void unsubscribe(const SubscriptionPtr& subscription);
    void publish(const std::string& message);
    void close_all();

private:
    void compact_locked();

    size_t max_queue_size_;
    TracedMutex mutex_;
    std::vector<std::weak_ptr<Subscription>> subscriptions_;
};

} // namespace keen_pbr3

#endif // WITH_API
