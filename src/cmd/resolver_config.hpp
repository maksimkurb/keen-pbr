#pragma once

#include <iosfwd>
#include <string>

namespace keen_pbr3 {

// `keen-pbr generate-resolver-config dnsmasq`: the output of dnsmasq's
// conf-script.  Decides from the config file alone (never from daemon state):
//   - dns.resolver_integration = dnsmasq: the generated config; when it has no
//     default upstream, the server= lines of `fallback_path` are appended;
//   - integration none, or the config is unreadable or invalid: only the
//     server= lines of `fallback_path`.
// The output always ends with a stamp record; the off/fallback output carries
// kDnsmasqOffStampHash.  Runs inside dnsmasq's startup, so it never fails:
// problems are reported as comment lines and in the log.  Returns 0.
int run_generate_resolver_config(const std::string& resolver_type,
                                 const std::string& config_path,
                                 const std::string& fallback_path,
                                 std::ostream& out);

} // namespace keen_pbr3
