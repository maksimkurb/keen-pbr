#pragma once

#include "intercept_processor.hpp"
#include "intercept_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
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

// A run of event sequence numbers [from_seq, to_seq] lost before delivery.
struct EventGap {
    uint64_t from_seq{0};
    uint64_t to_seq{0};
};

// `events` = result of events_since(forwarded_seq, ...): ascending.  Returns the
// gap between `forwarded_seq` and the first event, if the ring overwrote
// events that were never forwarded.
std::optional<EventGap> detect_event_gap(uint64_t forwarded_seq,
                                         const std::vector<InterceptEvent>& events);

// `GAP` event of the /api/dns/test stream.
nlohmann::json event_gap_to_json(const EventGap& gap);

// Notice for messages dropped between `first` and `last` (JSON event strings:
// INTERCEPT or GAP) of one subscriber queue; "" if their seq cannot be read.
std::string gap_notice_for_dropped(const std::string& first, const std::string& last);

// Parse /proc/net/netfilter/nfnetlink_queue text; returns the line for
// `queue_num` (columns: queue_number peer_portid queue_total copy_mode
// copy_range queue_dropped user_dropped id_sequence 1).
std::optional<api::KernelQueue> parse_nfnetlink_queue(const std::string& text, int queue_num);

// Reads the proc file; nullopt if missing or no line for the queue.
std::optional<api::KernelQueue> read_kernel_queue(int queue_num);

} // namespace keen_pbr3
