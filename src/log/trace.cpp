#include "trace.hpp"

#include "logger.hpp"

#include <atomic>
#include <chrono>

namespace keen_pbr3 {

namespace {

std::atomic<TraceId> g_next_trace_id{1};
thread_local TraceId g_current_trace_id = 0;

std::uint64_t now_mono_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

TraceId allocate_trace_id() {
    return g_next_trace_id.fetch_add(1, std::memory_order_relaxed);
}

TraceId current_trace_id() {
    return g_current_trace_id;
}

TraceId ensure_trace_id() {
    if (g_current_trace_id == 0) {
        g_current_trace_id = allocate_trace_id();
    }
    return g_current_trace_id;
}

TraceContext current_trace_context() {
    return TraceContext{current_trace_id()};
}

ScopedTraceContext::ScopedTraceContext(TraceId trace_id)
    : previous_trace_id_(g_current_trace_id) {
    g_current_trace_id = trace_id;
}

ScopedTraceContext::ScopedTraceContext(const TraceContext& trace_context)
    : ScopedTraceContext(trace_context.id) {}

ScopedTraceContext::~ScopedTraceContext() {
    g_current_trace_id = previous_trace_id_;
}

TraceSpan::TraceSpan(std::string label, TraceId trace_id)
    : label_(std::move(label))
    , trace_id_(trace_id == 0 ? ensure_trace_id() : trace_id)
    , started_at_ms_(now_mono_ms()) {
    ScopedTraceContext scope(trace_id_);
    Logger::instance().trace("span_start", "label={}", label_);
}

TraceSpan::~TraceSpan() {
    ScopedTraceContext scope(trace_id_);
    const auto duration_ms = now_mono_ms() - started_at_ms_;
    if (failed_) {
        Logger::instance().trace("span_error",
                                 "label={} duration_ms={} error={}",
                                 label_,
                                 duration_ms,
                                 failure_message_);
        return;
    }

    Logger::instance().trace("span_end", "label={} duration_ms={}", label_, duration_ms);
}

void TraceSpan::fail(std::string_view message) {
    failed_ = true;
    failure_message_ = std::string(message);
}

} // namespace keen_pbr3
