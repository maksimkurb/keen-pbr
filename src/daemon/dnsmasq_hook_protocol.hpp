#pragma once

#include "dnsmasq_types.hpp"

#include <string>
#include <string_view>

namespace keen_pbr3 {

struct HookInspectParse {
    bool ok{false};
    HookInspectResult result;
    std::string error;   // set when ok == false
};

// Parses the output of `dnsmasq-hook.sh inspect` (protocol v2). Never throws.
HookInspectParse parse_hook_inspect_output(std::string_view text);

} // namespace keen_pbr3
