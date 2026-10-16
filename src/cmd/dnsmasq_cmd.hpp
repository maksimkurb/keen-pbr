#pragma once

#include "../daemon/dnsmasq_types.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace keen_pbr3 {

// Returns the process exit code. Never throws.
int run_dnsmasq_command(const std::vector<std::string>& args,
                        const std::string& config_path,
                        const std::string& hook_path);

// `dnsmasq detect` text layout (SPEC §15.3). Warnings are not included
// ("warning: ..." lines are part of the text, after the instance list).
std::string format_dnsmasq_detection_text(const DnsmasqDetection& detection,
                                          const std::string& hook_path,
                                          const std::string& config_path);

// `dnsmasq detect --json` object.
nlohmann::json format_dnsmasq_detection_json(const DnsmasqDetection& detection);

// 0 when an instance is selected and its problem is None, 1 otherwise.
int dnsmasq_detection_exit_code(const DnsmasqDetection& detection);

} // namespace keen_pbr3
