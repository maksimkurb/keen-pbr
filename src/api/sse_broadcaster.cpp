#ifdef WITH_API

#include "sse_broadcaster.hpp"

#include "../log/logger.hpp"

namespace keen_pbr3 {

SseBroadcaster::SseBroadcaster(size_t max_queue_size)
    : max_queue_size_(max_queue_size) {}

SseBroadcaster::SubscriptionPtr SseBroadcaster::subscribe() {
    auto subscription = std::make_shared<Subscription>();
    KPBR_LOCK_GUARD(mutex_);
    subscriptions_.push_back(subscription);
    Logger::instance().trace("sse_subscribe", "subscriptions={}", subscriptions_.size());
    return subscription;
}

void SseBroadcaster::unsubscribe(const SubscriptionPtr& subscription) {
    if (!subscription) {
        return;
    }

    {
        KPBR_UNIQUE_LOCK(sub_lock, subscription->mutex);
        subscription->closed = true;
    }
    subscription->cv.notify_all();

    KPBR_LOCK_GUARD(mutex_);
    compact_locked();
    Logger::instance().trace("sse_unsubscribe", "subscriptions={}", subscriptions_.size());
}

void SseBroadcaster::publish(const std::string& message) {
    KPBR_LOCK_GUARD(mutex_);
    Logger::instance().trace("sse_publish", "subscriptions={} bytes={}", subscriptions_.size(), message.size());

    auto out = subscriptions_.begin();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        auto subscription = it->lock();
        if (!subscription) {
            continue;
        }

        bool keep = true;
        {
            KPBR_UNIQUE_LOCK(sub_lock, subscription->mutex);
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
        KPBR_LOCK_GUARD(mutex_);
        for (auto& weak : subscriptions_) {
            if (auto subscription = weak.lock()) {
                active.push_back(std::move(subscription));
            }
        }
        subscriptions_.clear();
    }

    for (auto& subscription : active) {
        {
            KPBR_UNIQUE_LOCK(sub_lock, subscription->mutex);
            subscription->closed = true;
        }
        subscription->cv.notify_all();
    }
    Logger::instance().trace("sse_close_all", "closed={}", active.size());
}

void SseBroadcaster::compact_locked() {
    auto out = subscriptions_.begin();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        auto subscription = it->lock();
        if (!subscription) {
            continue;
        }

        KPBR_UNIQUE_LOCK(sub_lock, subscription->mutex);
        if (subscription->closed) {
            continue;
        }

        *out++ = *it;
    }
    subscriptions_.erase(out, subscriptions_.end());
}

} // namespace keen_pbr3

#endif // WITH_API
