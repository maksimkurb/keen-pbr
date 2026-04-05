#include "traced_mutex.hpp"

#include "../log/logger.hpp"

#include <chrono>

namespace keen_pbr3 {

namespace {

constexpr auto kLockWaitLogInterval = std::chrono::milliseconds(250);

std::uint64_t mono_ms_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void log_lock_event(std::string_view event,
                    const char* mutex_name,
                    const char* mode,
                    const char* file,
                    int line,
                    const char* function,
                    std::uint64_t duration_ms = 0) {
    auto& log = Logger::instance();
    if (duration_ms == 0) {
        log.trace(event,
                  "mutex={} mode={} site={}:{} func={}",
                  mutex_name,
                  mode,
                  file,
                  line,
                  function);
        return;
    }

    log.trace(event,
              "mutex={} mode={} site={}:{} func={} duration_ms={}",
              mutex_name,
              mode,
              file,
              line,
              function,
              duration_ms);
}

template<typename LockFn, typename TryLockFn>
void lock_with_trace(LockFn lock_fn,
                     TryLockFn try_lock_fn,
                     const char* mutex_name,
                     const char* mode,
                     const char* file,
                     int line,
                     const char* function) {
    auto& log = Logger::instance();
    if (!log.is_enabled(LogLevel::debug)) {
        lock_fn();
        return;
    }

    const auto started_at = mono_ms_now();
    log_lock_event("lock_wait_start", mutex_name, mode, file, line, function);
    while (!try_lock_fn()) {
        log_lock_event("lock_waiting",
                       mutex_name,
                       mode,
                       file,
                       line,
                       function,
                       mono_ms_now() - started_at);
    }
    log_lock_event("lock_acquired",
                   mutex_name,
                   mode,
                   file,
                   line,
                   function,
                   mono_ms_now() - started_at);
}

void log_lock_release(const char* mutex_name,
                      const char* mode,
                      const char* file,
                      int line,
                      const char* function,
                      std::uint64_t acquired_at_ms) {
    if (acquired_at_ms == 0) {
        return;
    }
    log_lock_event("lock_released",
                   mutex_name,
                   mode,
                   file,
                   line,
                   function,
                   mono_ms_now() - acquired_at_ms);
}

} // namespace

void TracedMutex::lock(const char* mutex_name,
                       const char* file,
                       int line,
                       const char* function) {
    lock_with_trace(
        [this]() { mutex_.lock(); },
        [this]() { return mutex_.try_lock_for(kLockWaitLogInterval); },
        mutex_name,
        "exclusive",
        file,
        line,
        function);
}

void TracedMutex::unlock() {
    mutex_.unlock();
}

void TracedSharedMutex::lock(const char* mutex_name,
                             const char* file,
                             int line,
                             const char* function) {
    lock_with_trace(
        [this]() { mutex_.lock(); },
        [this]() { return mutex_.try_lock_for(kLockWaitLogInterval); },
        mutex_name,
        "exclusive",
        file,
        line,
        function);
}

void TracedSharedMutex::unlock() {
    mutex_.unlock();
}

void TracedSharedMutex::lock_shared(const char* mutex_name,
                                    const char* file,
                                    int line,
                                    const char* function) {
    lock_with_trace(
        [this]() { mutex_.lock_shared(); },
        [this]() { return mutex_.try_lock_shared_for(kLockWaitLogInterval); },
        mutex_name,
        "shared",
        file,
        line,
        function);
}

void TracedSharedMutex::unlock_shared() {
    mutex_.unlock_shared();
}

TracedLockGuard::TracedLockGuard(TracedMutex& mutex,
                                 const char* mutex_name,
                                 const char* file,
                                 int line,
                                 const char* function)
    : mutex_(&mutex)
    , mutex_name_(mutex_name)
    , file_(file)
    , line_(line)
    , function_(function) {
    mutex_->lock(mutex_name_, file_, line_, function_);
    acquired_at_ms_ = mono_ms_now();
}

TracedLockGuard::~TracedLockGuard() {
    log_lock_release(mutex_name_, "exclusive", file_, line_, function_, acquired_at_ms_);
    mutex_->unlock();
}

TracedUniqueLock::TracedUniqueLock(TracedMutex& mutex,
                                   const char* mutex_name,
                                   const char* file,
                                   int line,
                                   const char* function)
    : mutex_(&mutex)
    , mutex_name_(mutex_name)
    , file_(file)
    , line_(line)
    , function_(function) {
    lock();
}

TracedUniqueLock::~TracedUniqueLock() {
    if (owns_lock_) {
        unlock();
    }
}

void TracedUniqueLock::lock() {
    mutex_->lock(mutex_name_, file_, line_, function_);
    owns_lock_ = true;
    acquired_at_ms_ = mono_ms_now();
}

void TracedUniqueLock::unlock() {
    if (!owns_lock_) {
        return;
    }
    log_lock_release(mutex_name_, "exclusive", file_, line_, function_, acquired_at_ms_);
    mutex_->unlock();
    owns_lock_ = false;
    acquired_at_ms_ = 0;
}

bool TracedUniqueLock::owns_lock() const {
    return owns_lock_;
}

TracedSharedLock::TracedSharedLock(TracedSharedMutex& mutex,
                                   const char* mutex_name,
                                   const char* file,
                                   int line,
                                   const char* function)
    : mutex_(&mutex)
    , mutex_name_(mutex_name)
    , file_(file)
    , line_(line)
    , function_(function) {
    mutex_->lock_shared(mutex_name_, file_, line_, function_);
    acquired_at_ms_ = mono_ms_now();
}

TracedSharedLock::~TracedSharedLock() {
    log_lock_release(mutex_name_, "shared", file_, line_, function_, acquired_at_ms_);
    mutex_->unlock_shared();
}

TracedSharedUniqueLock::TracedSharedUniqueLock(TracedSharedMutex& mutex,
                                               const char* mutex_name,
                                               const char* file,
                                               int line,
                                               const char* function)
    : mutex_(&mutex)
    , mutex_name_(mutex_name)
    , file_(file)
    , line_(line)
    , function_(function) {
    lock();
}

TracedSharedUniqueLock::~TracedSharedUniqueLock() {
    if (owns_lock_) {
        unlock();
    }
}

void TracedSharedUniqueLock::lock() {
    mutex_->lock(mutex_name_, file_, line_, function_);
    owns_lock_ = true;
    acquired_at_ms_ = mono_ms_now();
}

void TracedSharedUniqueLock::unlock() {
    if (!owns_lock_) {
        return;
    }
    log_lock_release(mutex_name_, "exclusive", file_, line_, function_, acquired_at_ms_);
    mutex_->unlock();
    owns_lock_ = false;
    acquired_at_ms_ = 0;
}

bool TracedSharedUniqueLock::owns_lock() const {
    return owns_lock_;
}

} // namespace keen_pbr3
