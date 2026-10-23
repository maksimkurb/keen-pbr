#pragma once

#include "../util/term.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace keen_pbr3 {

// Per-list view of the daemon's `download` result:
// {"lists": [{"list", "status": changed|unchanged|failed, "error"?}],
//  "summary": {"refreshed", "changed", "unchanged", "failed"}, "reloaded"}.
nlohmann::json format_download_json(const nlohmann::json& result);

// The same result for people: one line per list and a summary line.
std::string format_download_text(const nlohmann::json& result, const term::Style& style);

} // namespace keen_pbr3
