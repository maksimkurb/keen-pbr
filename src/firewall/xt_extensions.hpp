#pragma once

#include <string>
#include <vector>

namespace keen_pbr3 {

struct PhysicalRuleset;

// One iptables extension (an entry of /proc/net/ip*_tables_{matches,targets})
// and the kernel module that registers it.
struct XtExtension {
  const char *name;   // "multiport", "MARK", ...
  bool target;        // true: ip*_tables_targets, false: ip*_tables_matches
  const char *module; // "xt_multiport", "xt_mark", ...
};

// Every extension the routing rules can render.  Interception's own
// extensions (NFQUEUE, NFLOG, connbytes) are checked by the intercept probe.
const std::vector<XtExtension> &routing_xt_extensions();

// "match multiport (xt_multiport)" / "target MARK (xt_mark)".
std::string xt_extension_label(const XtExtension &extension);

// The routing extensions `ruleset` uses when rendered for iptables-restore,
// in routing_xt_extensions() order.  Pointers into that list.
std::vector<const XtExtension *>
routing_xt_extensions_used(const PhysicalRuleset &ruleset);

// What the startup probe found, handed to the iptables backend once.
struct XtExtensionSupport {
  // /proc listings were readable; otherwise nothing is known and nothing is
  // enforced.
  bool checked{false};
  // A rule that needs a missing extension fails the apply before anything is
  // changed.  Off for nf_tables-backed iptables, which translates most
  // extensions natively and needs no xt module for them.
  bool enforce{false};
  // Pointers into routing_xt_extensions().
  std::vector<const XtExtension *> missing;
};

} // namespace keen_pbr3
