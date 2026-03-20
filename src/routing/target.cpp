#include "target.hpp"

namespace keen_pbr3 {

namespace {

// Find an outbound by its tag, returning a pointer or nullptr
const Outbound* find_outbound_by_tag(
    const std::vector<Outbound>& outbounds,
    const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (ob.tag == tag) {
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

    // IGNORE type means skip (not managed by keen-pbr)
    if (ob->type == OutboundType::IGNORE) {
        return RoutingDecision::skip();
    }

    return RoutingDecision::route_to(ob);
}

} // namespace keen_pbr3
