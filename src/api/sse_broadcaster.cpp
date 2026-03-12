#ifdef WITH_API

#include "sse_broadcaster.hpp"

namespace keen_pbr3 {

SseBroadcaster::SseBroadcaster(size_t max_queue_size)
    : max_queue_size_(max_queue_size) {}

SseBroadcaster::SubscriptionPtr SseBroadcaster::subscribe() {
    auto subscription = std::make_shared<Subscription>();
    std::lock_guard lock(mutex_);
    subscriptions_.push_back(subscription);
    return subscription;
}

void SseBroadcaster::unsubscribe(const SubscriptionPtr& subscription) {
    if (!subscription) {
        return;
    }

    {
        std::lock_guard sub_lock(subscription->mutex);
        subscription->closed = true;
    }
    subscription->cv.notify_all();

    std::lock_guard lock(mutex_);
    compact_locked();
}

void SseBroadcaster::publish(const std::string& message) {
    std::lock_guard lock(mutex_);

    auto out = subscriptions_.begin();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        auto subscription = it->lock();
        if (!subscription) {
            continue;
        }

        bool keep = true;
        {
            std::lock_guard sub_lock(subscription->mutex);
            if (subscription->closed) {
                keep = false;
            } else if (subscription->messages.size() >= max_queue_size_) {
                subscription->closed = true;
                keep = false;
            } else {
                subscription->messages.push_back(message);
            }
        }
        subscription->cv.notify_all();

        if (keep) {
            *out++ = *it;
        }
    }
    subscriptions_.erase(out, subscriptions_.end());
}

void SseBroadcaster::close_all() {
    std::vector<SubscriptionPtr> active;
    {
        std::lock_guard lock(mutex_);
        for (auto& weak : subscriptions_) {
            if (auto subscription = weak.lock()) {
                active.push_back(std::move(subscription));
            }
        }
        subscriptions_.clear();
    }

    for (auto& subscription : active) {
        {
            std::lock_guard sub_lock(subscription->mutex);
            subscription->closed = true;
        }
        subscription->cv.notify_all();
    }
}

void SseBroadcaster::compact_locked() {
    auto out = subscriptions_.begin();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        auto subscription = it->lock();
        if (!subscription) {
            continue;
        }

        std::lock_guard sub_lock(subscription->mutex);
        if (subscription->closed) {
            continue;
        }

        *out++ = *it;
    }
    subscriptions_.erase(out, subscriptions_.end());
}

} // namespace keen_pbr3

#endif // WITH_API
