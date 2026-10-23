#pragma once

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

// Per-list view of the daemon's `download` result:
// {"lists": [{"list", "status": changed|unchanged|failed, "error"?}],
//  "summary": {"refreshed", "changed", "unchanged", "failed"}, "reloaded"}.
nlohmann::json format_download_json(const nlohmann::json& result);

} // namespace keen_pbr3
