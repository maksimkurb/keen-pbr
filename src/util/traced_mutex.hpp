#pragma once

#include <keen-pbr/thread_annotations.hpp>

#include "../log/trace.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace keen_pbr3 {

class CAPABILITY("mutex") TracedMutex {
public:
    TracedMutex() = default;

    void lock(const char* mutex_name,
              const char* file,
              int line,
              const char* function) ACQUIRE();
    void unlock() RELEASE();

private:
    std::timed_mutex mutex_;
};

class SHARED_CAPABILITY("shared_mutex") TracedSharedMutex {
public:
    TracedSharedMutex() = default;

    void lock(const char* mutex_name,
              const char* file,
              int line,
              const char* function) ACQUIRE();
    void unlock() RELEASE();
    void lock_shared(const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function) ACQUIRE_SHARED();
    void unlock_shared() RELEASE_SHARED();

private:
    std::shared_timed_mutex mutex_;
};

class SCOPED_CAPABILITY TracedLockGuard {
public:
    TracedLockGuard(TracedMutex& mutex,
                    const char* mutex_name,
                    const char* file,
                    int line,
                    const char* function) ACQUIRE(mutex);
    ~TracedLockGuard() RELEASE();

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

class SCOPED_CAPABILITY TracedUniqueLock {
public:
    TracedUniqueLock(TracedMutex& mutex,
                     const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function) ACQUIRE(mutex);
    ~TracedUniqueLock() RELEASE();

    TracedUniqueLock(const TracedUniqueLock&) = delete;
    TracedUniqueLock& operator=(const TracedUniqueLock&) = delete;

    void lock() ACQUIRE();
    void unlock() RELEASE();
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

class SCOPED_CAPABILITY TracedSharedLock {
public:
    TracedSharedLock(TracedSharedMutex& mutex,
                     const char* mutex_name,
                     const char* file,
                     int line,
                     const char* function) ACQUIRE_SHARED(mutex);
    ~TracedSharedLock() RELEASE();

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

class SCOPED_CAPABILITY TracedSharedUniqueLock {
public:
    TracedSharedUniqueLock(TracedSharedMutex& mutex,
                           const char* mutex_name,
                           const char* file,
                           int line,
                           const char* function) ACQUIRE(mutex);
    ~TracedSharedUniqueLock() RELEASE();

    TracedSharedUniqueLock(const TracedSharedUniqueLock&) = delete;
    TracedSharedUniqueLock& operator=(const TracedSharedUniqueLock&) = delete;

    void lock() ACQUIRE();
    void unlock() RELEASE();
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
