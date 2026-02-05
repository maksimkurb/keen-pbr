#include "target.hpp"

namespace keen_pbr3 {

namespace {

// Helper to get the tag from any outbound variant
std::string get_outbound_tag(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Find an outbound by its tag, returning a pointer or nullptr
const Outbound* find_outbound_by_tag(
    const std::vector<Outbound>& outbounds,
    const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (get_outbound_tag(ob) == tag) {
            return &ob;
        }
    }
    return nullptr;
}

} // anonymous namespace

RoutingDecision resolve_route_action(
    const std::variant<std::string, std::vector<std::string>, SkipAction>& action,
    const std::vector<Outbound>& outbounds,
    const HealthCheckFn& health_fn) {

    return std::visit([&](const auto& act) -> RoutingDecision {
        using T = std::decay_t<decltype(act)>;

        if constexpr (std::is_same_v<T, SkipAction>) {
            return RoutingDecision::skip();
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            // Single outbound tag
            const Outbound* ob = find_outbound_by_tag(outbounds, act);
            if (!ob) {
                return RoutingDecision::none();
            }
            return RoutingDecision::route_to(ob);
        }
        else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            // Failover chain: select the first healthy outbound
            for (const auto& tag : act) {
                const Outbound* ob = find_outbound_by_tag(outbounds, tag);
                if (!ob) {
                    continue;
                }
                // If no health check function, all outbounds are considered healthy
                if (!health_fn || health_fn(tag)) {
                    return RoutingDecision::route_to(ob);
                }
            }
            // No healthy outbound found in chain
            return RoutingDecision::none();
        }
    }, action);
}

} // namespace keen_pbr3
