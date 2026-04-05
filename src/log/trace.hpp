#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace keen_pbr3 {

using TraceId = std::uint64_t;

struct TraceContext {
    TraceId id{0};
};

TraceId allocate_trace_id();
TraceId current_trace_id();
TraceId ensure_trace_id();
TraceContext current_trace_context();

class ScopedTraceContext {
public:
    explicit ScopedTraceContext(TraceId trace_id);
    explicit ScopedTraceContext(const TraceContext& trace_context);
    ~ScopedTraceContext();

    ScopedTraceContext(const ScopedTraceContext&) = delete;
    ScopedTraceContext& operator=(const ScopedTraceContext&) = delete;

private:
    TraceId previous_trace_id_{0};
};

class TraceSpan {
public:
    explicit TraceSpan(std::string label, TraceId trace_id = 0);
    ~TraceSpan();

    TraceSpan(const TraceSpan&) = delete;
    TraceSpan& operator=(const TraceSpan&) = delete;

    void fail(std::string_view message);

private:
    std::string label_;
    TraceId trace_id_{0};
    bool failed_{false};
    std::string failure_message_;
    std::uint64_t started_at_ms_{0};
};

} // namespace keen_pbr3
