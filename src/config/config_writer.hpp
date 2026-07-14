#pragma once

#include <string>

namespace keen_pbr3 {

// Durably replace a regular config file. Existing ownership and mode are
// preserved; a newly created config is private to its owner (0600).
void write_config_atomically(const std::string& config_path,
                             const std::string& body);

} // namespace keen_pbr3
