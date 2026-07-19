#include "operation_coordinator.hpp"

#include <utility>

namespace keen_pbr3 {

bool OperationCoordinator::try_begin(std::string operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_) return false;
    busy_ = true;
    operation_ = std::move(operation);
    return true;
}

void OperationCoordinator::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    busy_ = false;
    operation_.clear();
}

bool OperationCoordinator::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_;
}

std::string OperationCoordinator::operation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return operation_;
}

} // namespace keen_pbr3
