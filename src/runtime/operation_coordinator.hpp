#pragma once

#include <mutex>
#include <string>

namespace keen_pbr3 {

// Immediate-fail mutation lease. Unlike a blocking lock, concurrent callers
// receive busy immediately and never wait behind an ongoing transaction.
class OperationCoordinator {
public:
    bool try_begin(std::string operation);
    void finish();
    bool busy() const;
    std::string operation() const;

private:
    mutable std::mutex mutex_;
    bool busy_{false};
    std::string operation_;
};

} // namespace keen_pbr3
