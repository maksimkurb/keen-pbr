#pragma once

#ifdef WITH_API

#include "../api/generated/api_types.hpp"
#include "../config/config.hpp"
#include "../routing/netlink.hpp"
#include "../routing/urltest_manager.hpp"

#include <functional>
#include <optional>
#include <string>

namespace keen_pbr3 {

using UrltestStateLookupFn = std::function<std::optional<UrltestState>(const std::string&)>;

api::RuntimeOutboundsResponse build_runtime_outbounds_response(
    const Config& config,
    NetlinkManager& netlink,
    const UrltestStateLookupFn& urltest_state_lookup);

} // namespace keen_pbr3

#endif // WITH_API
