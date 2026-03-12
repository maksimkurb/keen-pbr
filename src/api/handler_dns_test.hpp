#pragma once

#ifdef WITH_API

#include "handlers.hpp"

namespace keen_pbr3 {

void register_dns_test_handler(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
