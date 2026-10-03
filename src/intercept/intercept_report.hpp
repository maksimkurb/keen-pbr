#pragma once

#include "../config/config.hpp"
#include "intercept_processor.hpp"
#include "intercept_settings.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

// `intercept` object of /api/health/service.  `counters` may be null (service
// not running).  Parts are reported active only while the service runs.
api::InterceptHealthClass make_intercept_health(const InterceptEffective& effective,
                                                bool running,
                                                const InterceptCounters* counters,
                                                uint64_t events_seq,
                                                bool snapshot_ready = true);

const char* intercept_source_name(InterceptSource source);

// One `INTERCEPT` event of the /api/dns/test stream.
nlohmann::json intercept_event_to_json(const InterceptEvent& event);

} // namespace keen_pbr3
