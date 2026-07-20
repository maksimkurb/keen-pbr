#pragma once

#ifdef WITH_API

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "../api/generated/api_types.hpp"

namespace keen_pbr3 {

// Resolve Linux interface names to the descriptions configured in KeeneticOS.
// Returns no value when the host is not Keenetic or RCI could not provide a
// valid interface snapshot. Successful partial mappings are preserved.
// Populate entries from the two-minute on-demand cache. A failed refresh
// leaves a previously successful map available to callers.
void populate_keenetic_interface_descriptions(
    api::RuntimeInterfaceInventoryResponse& response);

#ifdef KEEN_PBR3_TESTING
using KeeneticInterfaceFetchFn = std::function<std::string(
    const std::string& method, const std::string& url, const std::string& body)>;
using KeeneticInterfaceNowFn = std::function<std::chrono::steady_clock::time_point()>;

void set_keenetic_interface_fetcher_for_tests(KeeneticInterfaceFetchFn fetcher);
void set_keenetic_interface_now_fn_for_tests(KeeneticInterfaceNowFn now_fn);
void reset_keenetic_interface_test_state();
#endif

} // namespace keen_pbr3

#endif // WITH_API
