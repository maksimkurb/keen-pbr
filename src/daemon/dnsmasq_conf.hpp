#pragma once

#include "dnsmasq_types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// All -C/--conf-file values in argv, in order (forms: "-C X", "-CX",
// "--conf-file=X", "--conf-file X").
std::vector<std::string> conf_files_from_argv(const std::vector<std::string>& argv);

// Parses "dir[,ext...]" (value of conf-dir / -7).
ConfDirSpec parse_conf_dir_value(std::string_view value);

// dnsmasq's conf-dir file filter.
bool conf_dir_accepts(const ConfDirSpec& spec, std::string_view file_name);

struct DnsmasqConfReadLimits {
    int max_depth{4};                     // nested conf-file levels
    std::size_t max_files{64};
    std::size_t max_file_bytes{1u << 20}; // 1 MiB; larger files are skipped with a warning
};

// Builds the effective config from argv plus the files it loads.
// `skip_file` is never parsed (our own drop-in: it only holds conf-script).
DnsmasqEffectiveConfig read_effective_config(const std::vector<std::string>& argv,
                                             const std::string& default_conf_file,
                                             const std::string& skip_file,
                                             const DnsmasqConfReadLimits& limits = {});

} // namespace keen_pbr3
