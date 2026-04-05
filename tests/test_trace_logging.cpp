#include <doctest/doctest.h>

#include "../src/log/logger.hpp"
#include "../src/log/trace.hpp"
#include "../src/util/blocking_executor.hpp"
#include "../src/util/safe_exec.hpp"
#include "../src/util/traced_mutex.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace keen_pbr3 {

namespace {

class LoggerCapture {
public:
    LoggerCapture() : previous_level_(Logger::instance().level()) {
        Logger::instance().set_level(LogLevel::debug);
        Logger::instance().set_sink([this](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back(line);
        });
    }

    ~LoggerCapture() {
        Logger::instance().clear_sink();
        Logger::instance().set_level(previous_level_);
    }

    bool contains(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::any_of(lines_.begin(), lines_.end(), [&needle](const std::string& line) {
            return line.find(needle) != std::string::npos;
        });
    }

private:
    LogLevel previous_level_;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

} // namespace

TEST_CASE("trace logger includes trace id and event metadata") {
    LoggerCapture capture;
    const auto trace_id = allocate_trace_id();

    {
        ScopedTraceContext scope(trace_id);
        Logger::instance().trace("unit-test-event", "value={}", 42);
    }

    CHECK(capture.contains("[T]"));
    CHECK(capture.contains("event=unit-test-event"));
    CHECK(capture.contains("trace=" + std::to_string(trace_id)));
    CHECK(capture.contains("value=42"));
}

TEST_CASE("blocking executor emits queue and completion trace events") {
    LoggerCapture capture;
    BlockingExecutor executor(1, 4);

    auto future = executor.submit(
        "trace-test-task",
        []() { return 7; },
        allocate_trace_id());

    CHECK(future.get() == 7);
    CHECK(capture.contains("event=executor_queue"));
    CHECK(capture.contains("label=trace-test-task"));
    CHECK(capture.contains("event=executor_start"));
    CHECK(capture.contains("event=executor_end"));
}

TEST_CASE("blocking executor rejects new tasks after shutdown") {
    BlockingExecutor executor(1, 4);
    executor.shutdown();

    CHECK_FALSE(executor.try_post("late-task", []() {}));

    auto future = executor.submit("late-submit", []() { return 7; });
    CHECK_THROWS(future.get());
}

TEST_CASE("traced mutex logs lock lifecycle and supports condition_variable_any") {
    LoggerCapture capture;
    TracedMutex mutex;
    std::condition_variable_any cv;
    bool ready = false;

    std::thread notifier([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            KPBR_UNIQUE_LOCK(lock, mutex);
            ready = true;
        }
        cv.notify_all();
    });

    {
        KPBR_UNIQUE_LOCK(lock, mutex);
        CHECK(cv.wait_for(lock, std::chrono::milliseconds(500), [&ready]() {
            return ready;
        }));
    }

    notifier.join();

    CHECK(capture.contains("event=lock_wait_start"));
    CHECK(capture.contains("event=lock_acquired"));
    CHECK(capture.contains("event=lock_released"));
}

TEST_CASE("traced mutex logs waiting during contention") {
    LoggerCapture capture;
    TracedMutex mutex;

    std::thread waiter;
    {
        KPBR_UNIQUE_LOCK(holder, mutex);
        waiter = std::thread([&]() {
            KPBR_UNIQUE_LOCK(waiting_lock, mutex);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }

    waiter.join();
    CHECK(capture.contains("event=lock_waiting"));
}

TEST_CASE("safe_exec capture emits trace events") {
    LoggerCapture capture;

    const auto result = safe_exec_capture({"/bin/echo", "hello"}, true);

    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "hello\n");
    CHECK(capture.contains("event=safe_exec_capture_start"));
    CHECK(capture.contains("event=safe_exec_capture_end"));
}

} // namespace keen_pbr3
