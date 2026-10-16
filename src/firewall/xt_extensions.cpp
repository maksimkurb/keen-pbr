#include "xt_extensions.hpp"

#include "firewall_physical.hpp"

#include <algorithm>
#include <cstring>
#include <variant>

namespace keen_pbr3 {

const std::vector<XtExtension> &routing_xt_extensions() {
  // xt_mark and xt_connmark register both the match and the target.
  static const std::vector<XtExtension> kExtensions{
      {"set", false, "xt_set"},
      {"mark", false, "xt_mark"},
      {"MARK", true, "xt_mark"},
      {"connmark", false, "xt_connmark"},
      {"CONNMARK", true, "xt_connmark"},
      {"conntrack", false, "xt_conntrack"},
      {"tcp", false, "xt_tcpudp"},
      {"udp", false, "xt_tcpudp"},
      {"multiport", false, "xt_multiport"},
      {"dscp", false, "xt_dscp"},
      {"addrtype", false, "xt_addrtype"},
      {"comment", false, "xt_comment"},
#ifndef KEEN_PBR_PLATFORM_KEENETIC
      {"statistic", false, "xt_statistic"},
#endif
  };
  return kExtensions;
}

std::string xt_extension_label(const XtExtension &extension) {
  return std::string(extension.target ? "target " : "match ") +
         extension.name + " (" + extension.module + ")";
}

namespace {

class UsedExtensions {
public:
  void add(const char *name, bool target) {
    const auto &all = routing_xt_extensions();
    for (std::size_t index = 0; index < all.size(); ++index) {
      if (all[index].target == target &&
          std::strcmp(all[index].name, name) == 0) {
        used_.push_back(index);
        return;
      }
    }
  }

  std::vector<const XtExtension *> sorted() {
    std::sort(used_.begin(), used_.end());
    used_.erase(std::unique(used_.begin(), used_.end()), used_.end());
    std::vector<const XtExtension *> out;
    for (const std::size_t index : used_) {
      out.push_back(&routing_xt_extensions()[index]);
    }
    return out;
  }

private:
  std::vector<std::size_t> used_;
};

// Mirrors append_iptables_match: a single port is `-p <proto> --dport`, which
// loads the protocol's implicit match; a list needs multiport.
void add_match(UsedExtensions &used, const PhysicalRule &rule,
               const PhysicalMatch &match) {
  if (std::holds_alternative<SetMatch>(match)) {
    used.add("set", false);
  } else if (const auto *port = std::get_if<PortMatch>(&match)) {
    if (port->ranges.size() > 1U) {
      used.add("multiport", false);
      return;
    }
    for (const auto &other : rule.matches) {
      if (const auto *proto = std::get_if<ProtoMatch>(&other)) {
        used.add(l4_proto_name(proto->proto), false);
      }
    }
  } else if (std::holds_alternative<DscpMatch>(match)) {
    used.add("dscp", false);
  } else if (std::holds_alternative<AddrTypeMatch>(match)) {
    used.add("addrtype", false);
  } else if (const auto *mark = std::get_if<MarkMatch>(&match)) {
    used.add(mark->kind == PhysicalMarkKind::conntrack ? "connmark" : "mark",
             false);
  } else if (std::holds_alternative<CtStateMatch>(match) ||
             std::holds_alternative<CtDirMatch>(match)) {
    used.add("conntrack", false);
  } else if (std::holds_alternative<StatisticMatch>(match)) {
    used.add("statistic", false);
  }
}

void add_statement(UsedExtensions &used, const PhysicalStatement &statement) {
  if (const auto *mark = std::get_if<SetMarkStmt>(&statement)) {
    used.add(mark->kind == PhysicalMarkKind::conntrack ? "CONNMARK" : "MARK",
             true);
  } else if (std::holds_alternative<CopyMarkStmt>(statement)) {
    used.add("CONNMARK", true);
  }
}

} // namespace

std::vector<const XtExtension *>
routing_xt_extensions_used(const PhysicalRuleset &ruleset) {
  UsedExtensions used;
  for (const auto &chain : ruleset.chains) {
    for (const auto &rule : chain.rules) {
      for (const auto &match : rule.matches) {
        add_match(used, rule, match);
      }
      for (const auto &statement : rule.statements) {
        add_statement(used, statement);
      }
      // render_iptables_rule appends `-m comment` for keyed rules.
      if (rule.key.has_value()) {
        used.add("comment", false);
      }
    }
  }
  return used.sorted();
}

} // namespace keen_pbr3
