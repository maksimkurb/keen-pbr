#include "runtime_state_store.hpp"

namespace keen_pbr3 {

RuntimeStateSnapshot RuntimeStateStore::snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return snapshot_;
}

void RuntimeStateStore::publish(RuntimeStateSnapshot snapshot) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    snapshot_ = std::move(snapshot);
}

} // namespace keen_pbr3
