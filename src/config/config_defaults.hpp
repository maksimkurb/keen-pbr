#pragma once

#include <cstdint>
#include <string_view>

// What the daemon uses for a config value that is not set. GET
// /api/config/defaults serves them to the web UI (make_config_defaults()):
// a new default goes here and there.
namespace keen_pbr3::config_defaults {

// daemon
constexpr bool strict_enforcement = false;
constexpr bool skip_marked_packets = true;
constexpr bool clear_dynamic_sets_on_apply = false;
constexpr bool reuse_static_sets_on_runtime_refresh = true;
constexpr bool ipv6_enabled = true;
// Not passed to ipset when unset; these are ipset's own defaults.
constexpr int64_t ipset_hashsize = 1024;
constexpr int64_t ipset_maxelem = 65536;

// fwmark
constexpr uint32_t fwmark_start = 0x00010000;
constexpr uint32_t fwmark_mask = 0x00FF0000;

// iproute
constexpr int64_t table_start = 150;
constexpr bool process_router_traffic = false;

// lists_autoupdate
constexpr bool lists_autoupdate_enabled = false;

// intercept
constexpr bool intercept_enabled = true;
constexpr int64_t intercept_min_ttl_ms = 300000;
constexpr int64_t intercept_max_ttl_ms = 86400000;
constexpr bool intercept_dns_enabled = true;
constexpr int64_t intercept_dns_queue_num = 9053;
constexpr int64_t intercept_dns_hold_timeout_ms = 30;
constexpr std::string_view intercept_marker_domain = "check.keen-pbr.fyi";
constexpr std::string_view intercept_marker_answer_ipv4 = "127.0.0.88";
constexpr bool intercept_l7_enabled = true;
constexpr int64_t intercept_l7_nflog_group = 9054;
constexpr bool intercept_l7_tls = true;
constexpr bool intercept_l7_http = true;
constexpr bool intercept_l7_quic = true;

} // namespace keen_pbr3::config_defaults
