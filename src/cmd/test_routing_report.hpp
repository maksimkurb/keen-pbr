#pragma once

#include "test_routing.hpp"
#include "../util/term.hpp"

#include <string>

namespace keen_pbr3 {

// The test-routing result as the web UI's step trace: a verdict line, then DNS
// resolution, list matching, rule selection and the firewall check, then what
// the verdict means and what to do. The wording follows the diagnosis codes
// the daemon puts on each entry; nothing is re-derived here. A result where
// every address routes as expected shrinks to one line per step.
std::string format_test_routing_report(const TestRoutingResult& result,
                                       const TestRoutingCriteria& criteria,
                                       const term::Style& style);

} // namespace keen_pbr3
