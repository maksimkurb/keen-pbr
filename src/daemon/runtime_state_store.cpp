#include "runtime_state_store.hpp"

#include <mutex>

namespace keen_pbr3 {

RuntimeStateSnapshot RuntimeStateStore::snapshot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return snapshot_;
}

void RuntimeStateStore::publish(RuntimeStateSnapshot snapshot) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
}

} // namespace keen_pbr3
