#include "blocking_executor.hpp"

#include "../log/logger.hpp"

#include <chrono>

namespace keen_pbr3 {

BlockingExecutor::BlockingExecutor(std::size_t worker_count, std::size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers_.emplace_back([this, index]() {
            worker_loop(index);
        });
    }
}

BlockingExecutor::~BlockingExecutor() {
    shutdown();
}

void BlockingExecutor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_complete_) {
            return;
        }
        stopping_ = true;
        shutdown_complete_ = true;
    }
    cv_.notify_all();
    space_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool BlockingExecutor::try_post(std::string label,
                                std::function<void()> task,
                                TraceId trace_id) {
    return enqueue(std::move(label),
                   std::move(task),
                   trace_id,
                   /*block_until_room=*/false);
}

bool BlockingExecutor::enqueue(std::string label,
                               std::function<void()> task,
                               TraceId trace_id,
                               bool block_until_room) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (block_until_room) {
        space_cv_.wait(lock, [this]() {
            return stopping_ || queue_.size() < max_queue_size_;
        });
    }

    if (stopping_ || queue_.size() >= max_queue_size_) {
        Logger::instance().trace("executor_skip",
                                 "label={} reason={}",
                                 label,
                                 stopping_ ? "stopping" : "queue_full");
        return false;
    }

    queue_.push(Task{
        .label = std::move(label),
        .callback = std::move(task),
        .trace_id = trace_id,
    });
    Logger::instance().trace("executor_queue",
                             "queue_size={} label={}",
                             queue_.size(),
                             queue_.back().label);
    lock.unlock();
    cv_.notify_one();
    return true;
}

void BlockingExecutor::worker_loop(std::size_t worker_index) {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stopping_ || !queue_.empty();
            });
            if (stopping_ && queue_.empty()) {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop();
            space_cv_.notify_one();
        }

        const auto started_at = std::chrono::steady_clock::now();
        ScopedTraceContext scope(task.trace_id);
        Logger::instance().trace("executor_start",
                                 "worker={} label={}",
                                 worker_index,
                                 task.label);
        try {
            task.callback();
            const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count();
            Logger::instance().trace("executor_end",
                                     "worker={} label={} duration_ms={}",
                                     worker_index,
                                     task.label,
                                     duration_ms);
        } catch (const std::exception& e) {
            const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count();
            Logger::instance().trace("executor_error",
                                     "worker={} label={} duration_ms={} error={}",
                                     worker_index,
                                     task.label,
                                     duration_ms,
                                     e.what());
        } catch (...) {
            const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count();
            Logger::instance().trace("executor_error",
                                     "worker={} label={} duration_ms={} error=unknown",
                                     worker_index,
                                     task.label,
                                     duration_ms);
        }
    }
}

} // namespace keen_pbr3
