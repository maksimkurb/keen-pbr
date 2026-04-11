#pragma once

#ifdef WITH_API

#include "../api/generated/api_types.hpp"
#include "../routing/netlink.hpp"

namespace keen_pbr3 {

api::RuntimeInterfaceInventoryResponse build_runtime_interface_inventory_response(
    std::vector<DumpedInterface> dumped_interfaces);

api::RuntimeInterfaceInventoryResponse build_runtime_interface_inventory_response(
    NetlinkManager& netlink);

} // namespace keen_pbr3

#endif // WITH_API
