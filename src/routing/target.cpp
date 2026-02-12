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
    const std::string& outbound_tag,
    const std::vector<Outbound>& outbounds) {

    const Outbound* ob = find_outbound_by_tag(outbounds, outbound_tag);
    if (!ob) {
        return RoutingDecision::none();
    }

    // IgnoreOutbound means skip (not managed by keen-pbr3)
    if (std::holds_alternative<IgnoreOutbound>(*ob)) {
        return RoutingDecision::skip();
    }

    return RoutingDecision::route_to(ob);
}

} // namespace keen_pbr3
