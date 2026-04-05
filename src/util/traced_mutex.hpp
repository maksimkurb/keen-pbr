#pragma once

#include "../log/trace.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace keen_pbr3 {

class TracedMutex {
public:
    TracedMutex() = default;

    void lock(const char* mutex_name,
              const char* file,
              int line,
              const char* function);
    void unlock();

private:
    std::timed_mutex mutex_;
};

class TracedSharedMutex {
public:
    TracedSharedMutex() = default;

    void lock(const char* mutex_name,
              const char* file,
              int line,
              const char* function);
    void unlock();
    void lock_shared(const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function);
    void unlock_shared();

private:
    std::shared_timed_mutex mutex_;
};

class TracedLockGuard {
public:
    TracedLockGuard(TracedMutex& mutex,
                    const char* mutex_name,
                    const char* file,
                    int line,
                    const char* function);
    ~TracedLockGuard();

    TracedLockGuard(const TracedLockGuard&) = delete;
    TracedLockGuard& operator=(const TracedLockGuard&) = delete;

private:
    TracedMutex* mutex_;
    const char* mutex_name_;
    const char* file_;
    int line_;
    const char* function_;
    std::uint64_t acquired_at_ms_{0};
};

class TracedUniqueLock {
public:
    TracedUniqueLock(TracedMutex& mutex,
                     const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function);
    ~TracedUniqueLock();

    TracedUniqueLock(const TracedUniqueLock&) = delete;
    TracedUniqueLock& operator=(const TracedUniqueLock&) = delete;

    void lock();
    void unlock();
    bool owns_lock() const;

private:
    TracedMutex* mutex_;
    const char* mutex_name_;
    const char* file_;
    int line_;
    const char* function_;
    bool owns_lock_{false};
    std::uint64_t acquired_at_ms_{0};
};

class TracedSharedLock {
public:
    TracedSharedLock(TracedSharedMutex& mutex,
                     const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function);
    ~TracedSharedLock();

    TracedSharedLock(const TracedSharedLock&) = delete;
    TracedSharedLock& operator=(const TracedSharedLock&) = delete;

private:
    TracedSharedMutex* mutex_;
    const char* mutex_name_;
    const char* file_;
    int line_;
    const char* function_;
    std::uint64_t acquired_at_ms_{0};
};

class TracedSharedUniqueLock {
public:
    TracedSharedUniqueLock(TracedSharedMutex& mutex,
                           const char* mutex_name,
                           const char* file,
                           int line,
                           const char* function);
    ~TracedSharedUniqueLock();

    TracedSharedUniqueLock(const TracedSharedUniqueLock&) = delete;
    TracedSharedUniqueLock& operator=(const TracedSharedUniqueLock&) = delete;

    void lock();
    void unlock();
    bool owns_lock() const;

private:
    TracedSharedMutex* mutex_;
    const char* mutex_name_;
    const char* file_;
    int line_;
    const char* function_;
    bool owns_lock_{false};
    std::uint64_t acquired_at_ms_{0};
};

#define KPBR_CONCAT_IMPL(a, b) a##b
#define KPBR_CONCAT(a, b) KPBR_CONCAT_IMPL(a, b)
#define KPBR_LOCK_GUARD(mutex_expr) \
    ::keen_pbr3::TracedLockGuard KPBR_CONCAT(_kpbr_lock_guard_, __LINE__)( \
        (mutex_expr), #mutex_expr, __FILE__, __LINE__, __func__)
#define KPBR_UNIQUE_LOCK(name, mutex_expr) \
    ::keen_pbr3::TracedUniqueLock name((mutex_expr), #mutex_expr, __FILE__, __LINE__, __func__)
#define KPBR_SHARED_LOCK(name, mutex_expr) \
    ::keen_pbr3::TracedSharedLock name((mutex_expr), #mutex_expr, __FILE__, __LINE__, __func__)
#define KPBR_SHARED_UNIQUE_LOCK(name, mutex_expr) \
    ::keen_pbr3::TracedSharedUniqueLock name((mutex_expr), #mutex_expr, __FILE__, __LINE__, __func__)

} // namespace keen_pbr3
