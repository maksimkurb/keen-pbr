#pragma once

#ifdef WITH_API

#include "server.hpp"

namespace keen_pbr3 {

void register_diagnostics_handler(ApiServer& server);

} // namespace keen_pbr3

#endif // WITH_API
