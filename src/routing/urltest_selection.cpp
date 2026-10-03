#include "urltest_manager.hpp"
#include "../config/icmptest_limits.hpp"

#include <algorithm>
#include <limits>

namespace keen_pbr3 {

std::vector<std::string> select_test_group_usable_outbounds(const UrltestState& state) {
    const auto& test_group = state.config;
    if (!test_group.outbound_groups.has_value()) return {};

    struct GroupRef {
        size_t index;
        uint32_t weight;
    };
    const auto& groups = *test_group.outbound_groups;
    std::vector<GroupRef> sorted_groups;
    sorted_groups.reserve(groups.size());
    for (size_t index = 0; index < groups.size(); ++index) {
        sorted_groups.push_back({index,
            static_cast<uint32_t>(groups[index].weight.value_or(1))});
    }
    std::stable_sort(sorted_groups.begin(), sorted_groups.end(),
                     [](const GroupRef& left, const GroupRef& right) {
                         return left.weight < right.weight;
                     });

    for (const auto& group_ref : sorted_groups) {
        const auto group_tags = outbound_group_tags(groups[group_ref.index]);
        const auto usable = [&](const std::string& child_tag) {
            const auto breaker = state.circuit_breakers.find(child_tag);
            const auto result = state.last_results.find(child_tag);
            return breaker != state.circuit_breakers.end() &&
                breaker->second.state(child_tag) != CircuitState::open &&
                result != state.last_results.end() && result->second.success;
        };
        std::vector<std::string> usable_tags;
        for (const auto& child_tag : group_tags) {
            if (usable(child_tag)) {
                usable_tags.push_back(child_tag);
            }
        }
        if (!usable_tags.empty()) return usable_tags;
    }
    return {};
}

std::string select_test_group_outbound(const UrltestState& state) {
    const auto usable_tags = select_test_group_usable_outbounds(state);
    if (usable_tags.empty()) return {};

    uint32_t minimum_latency = std::numeric_limits<uint32_t>::max();
    for (const auto& child_tag : usable_tags) {
        minimum_latency = std::min(minimum_latency,
            state.last_results.at(child_tag).latency_ms);
    }

    const auto& test_group = state.config;
    const uint32_t tolerance = static_cast<uint32_t>(
        test_group.tolerance_ms.value_or(
            test_group.type == OutboundType::ICMPTEST
                ? icmptest_limits::default_tolerance_ms
                : 100));
    if (std::find(usable_tags.begin(), usable_tags.end(), state.selected_outbound) !=
            usable_tags.end() &&
        state.last_results.at(state.selected_outbound).latency_ms <=
            minimum_latency + tolerance) {
        return state.selected_outbound;
    }
    for (const auto& child_tag : usable_tags) {
        if (state.last_results.at(child_tag).latency_ms <= minimum_latency + tolerance) {
            return child_tag;
        }
    }
    return {};
}

} // namespace keen_pbr3
