#ifdef WITH_API

#include "runtime_interface_inventory.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {

api::RuntimeInterfaceInventoryStatusEnum map_interface_status(bool admin_up) {
    return admin_up
        ? api::RuntimeInterfaceInventoryStatusEnum::UP
        : api::RuntimeInterfaceInventoryStatusEnum::DOWN;
}

api::RuntimeInterfaceInventoryEntry build_runtime_interface_inventory_entry(
    DumpedInterface dumped) {
    api::RuntimeInterfaceInventoryEntry entry;
    entry.name = std::move(dumped.name);
    entry.status = map_interface_status(dumped.admin_up);
    entry.admin_up = dumped.admin_up;

    if (dumped.oper_state.has_value()) {
        entry.oper_state = std::move(dumped.oper_state);
    }
    if (dumped.carrier.has_value()) {
        entry.carrier = dumped.carrier;
    }
    if (!dumped.ipv4_addresses.empty()) {
        entry.ipv4_addresses = std::move(dumped.ipv4_addresses);
    }
    if (!dumped.ipv6_addresses.empty()) {
        entry.ipv6_addresses = std::move(dumped.ipv6_addresses);
    }

    return entry;
}

} // namespace

api::RuntimeInterfaceInventoryResponse build_runtime_interface_inventory_response(
    std::vector<DumpedInterface> dumped_interfaces) {
    api::RuntimeInterfaceInventoryResponse response;

    for (auto& dumped : dumped_interfaces) {
        response.interfaces.push_back(
            build_runtime_interface_inventory_entry(std::move(dumped)));
    }

    return response;
}

api::RuntimeInterfaceInventoryResponse build_runtime_interface_inventory_response(
    NetlinkManager& netlink) {
    return build_runtime_interface_inventory_response(netlink.dump_interfaces());
}

} // namespace keen_pbr3

#endif // WITH_API
