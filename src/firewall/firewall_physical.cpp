#include "firewall_physical.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

// ===========================================================================
// Identity, lookup
// ===========================================================================

bool PhysicalChainId::operator==(const PhysicalChainId &other) const {
  if (role != other.role || table != other.table || family != other.family ||
      setter_mark != other.setter_mark) {
    return false;
  }
  if (role == PhysicalChainRole::other_owned ||
      role == PhysicalChainRole::system_other) {
    return name == other.name;
  }
  return true;
}

const PhysicalChain *PhysicalRuleset::find(const PhysicalChainId &id) const {
  for (const auto &chain : chains) {
    if (chain.id == id) {
      return &chain;
    }
  }
  return nullptr;
}

bool PhysicalRuleset::operator==(const PhysicalRuleset &other) const {
  if (chains.size() != other.chains.size()) return false;
  for (const auto &chain : chains) {
    const PhysicalChain *match = other.find(chain.id);
    if (match == nullptr || !(*match == chain)) return false;
  }
  return true;
}

void append_physical_ruleset(PhysicalRuleset &into, PhysicalRuleset &&from) {
  for (auto &chain : from.chains) {
    PhysicalChain *existing = nullptr;
    for (auto &candidate : into.chains) {
      if (candidate.id == chain.id) {
        existing = &candidate;
        break;
      }
    }
    if (existing == nullptr) {
      into.chains.push_back(std::move(chain));
      continue;
    }
    for (auto &rule : chain.rules) {
      existing->rules.push_back(std::move(rule));
    }
  }
}

// ===========================================================================
// Canonicalization
// ===========================================================================

namespace {

struct Cidr {
  bool v6{false};
  std::array<uint8_t, 16> addr{};
  uint8_t len{0};
};

bool parse_cidr(std::string_view text, Cidr &out) {
  const auto slash = text.find('/');
  const std::string_view host = text.substr(0, slash);
  if (host.empty() || host.size() >= INET6_ADDRSTRLEN) {
    return false;
  }
  char buffer[INET6_ADDRSTRLEN];
  std::memcpy(buffer, host.data(), host.size());
  buffer[host.size()] = '\0';
  out = Cidr{};

  // Try IPv4 first, then IPv6
  in_addr a4{};
  if (inet_pton(AF_INET, buffer, &a4) == 1) {
    out.v6 = false;
    std::memcpy(out.addr.data(), &a4, 4);
  } else {
    in6_addr a6{};
    if (inet_pton(AF_INET6, buffer, &a6) == 1) {
      out.v6 = true;
      std::memcpy(out.addr.data(), &a6, 16);
    } else {
      return false;
    }
  }

  const unsigned max_len = out.v6 ? 128U : 32U;
  unsigned len = max_len;
  if (slash != std::string_view::npos) {
    const std::string_view digits = text.substr(slash + 1);
    const auto result =
        std::from_chars(digits.data(), digits.data() + digits.size(), len);
    if (digits.empty() || result.ec != std::errc{} ||
        result.ptr != digits.data() + digits.size() || len > max_len) {
      return false;
    }
  }
  out.len = static_cast<uint8_t>(len);
  // Clear host bits.
  for (unsigned bit = len; bit < max_len; ++bit) {
    out.addr[bit / 8U] &= static_cast<uint8_t>(~(0x80U >> (bit % 8U)));
  }
  return true;
}

std::string format_cidr(const Cidr &cidr) {
  char buffer[INET6_ADDRSTRLEN] = {};
  inet_ntop(cidr.v6 ? AF_INET6 : AF_INET, cidr.addr.data(), buffer,
            sizeof(buffer));
  std::string text = buffer;
  text.push_back('/');
  text += std::to_string(static_cast<unsigned>(cidr.len));
  return text;
}

bool cidr_less(const Cidr &a, const Cidr &b) {
  if (a.v6 != b.v6) return !a.v6;
  const int cmp = std::memcmp(a.addr.data(), b.addr.data(), 16);
  if (cmp != 0) return cmp < 0;
  return a.len < b.len;
}

bool cidr_covers(const Cidr &outer, const Cidr &inner) {
  if (outer.v6 != inner.v6 || outer.len > inner.len) return false;
  const unsigned full = outer.len / 8U;
  if (std::memcmp(outer.addr.data(), inner.addr.data(), full) != 0) return false;
  const unsigned rest = outer.len % 8U;
  if (rest == 0) return true;
  const uint8_t mask = static_cast<uint8_t>(0xFFU << (8U - rest));
  return (outer.addr[full] & mask) == (inner.addr[full] & mask);
}

bool cidr_bit(const Cidr &cidr, unsigned bit) {
  return (cidr.addr[bit / 8U] & (0x80U >> (bit % 8U))) != 0;
}

// True when `a` and `b` (same length, both > 0) are the two halves of one
// shorter prefix.
bool cidr_siblings(const Cidr &a, const Cidr &b) {
  if (a.v6 != b.v6 || a.len != b.len || a.len == 0) return false;
  for (unsigned bit = 0; bit + 1U < a.len; ++bit) {
    if (cidr_bit(a, bit) != cidr_bit(b, bit)) return false;
  }
  return cidr_bit(a, a.len - 1U) != cidr_bit(b, a.len - 1U);
}

// Canonical (sorted, non-overlapping) entries of one family whose union is
// the whole address space.
bool parsed_cidrs_cover_family(const std::vector<std::string> &canonical) {
  if (canonical.empty()) return false;
  std::vector<Cidr> stack;
  stack.reserve(canonical.size());
  for (const auto &text : canonical) {
    Cidr cidr;
    if (!parse_cidr(text, cidr)) return false;
    if (!stack.empty() && stack.back().v6 != cidr.v6) return false;
    stack.push_back(cidr);
    while (stack.size() >= 2U &&
           cidr_siblings(stack[stack.size() - 2U], stack.back())) {
      stack.pop_back();
      Cidr &parent = stack.back();
      --parent.len;
      for (unsigned bit = parent.len; bit < (parent.v6 ? 128U : 32U); ++bit) {
        parent.addr[bit / 8U] &= static_cast<uint8_t>(~(0x80U >> (bit % 8U)));
      }
    }
  }
  return stack.size() == 1U && stack.front().len == 0;
}

template <typename T> void sort_unique(std::vector<T> &values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

void canonicalize_match(PhysicalMatch &match) {
  if (auto *addr = std::get_if<AddrMatch>(&match)) {
    canonicalize_cidr_list(addr->cidrs);
  } else if (auto *port = std::get_if<PortMatch>(&match)) {
    canonicalize_port_ranges(port->ranges);
  } else if (auto *iif = std::get_if<IifMatch>(&match)) {
    sort_unique(iif->names);
  } else if (auto *mark = std::get_if<MarkMatch>(&match)) {
    sort_unique(mark->values);
  }
}

} // namespace

std::optional<std::string> canonical_cidr(std::string_view text) {
  Cidr cidr;
  if (!parse_cidr(text, cidr)) return std::nullopt;
  return format_cidr(cidr);
}

void canonicalize_cidr_list(std::vector<std::string> &cidrs) {
  struct Entry {
    bool parsed;
    Cidr cidr;
    std::string text;
  };
  std::vector<Entry> entries;
  entries.reserve(cidrs.size());
  for (auto &text : cidrs) {
    Entry entry{false, {}, std::move(text)};
    entry.parsed = parse_cidr(entry.text, entry.cidr);
    entries.push_back(std::move(entry));
  }
  // Unparseable text sorts last and is kept verbatim so it can never match.
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry &a, const Entry &b) {
                     if (a.parsed != b.parsed) return a.parsed;
                     return a.parsed ? cidr_less(a.cidr, b.cidr) : a.text < b.text;
                   });
  cidrs.clear();
  const Cidr *last = nullptr;
  for (const auto &entry : entries) {
    if (!entry.parsed) {
      if (cidrs.empty() || cidrs.back() != entry.text) cidrs.push_back(entry.text);
      continue;
    }
    if (last != nullptr && cidr_covers(*last, entry.cidr)) continue;
    last = &entry.cidr;
    cidrs.push_back(format_cidr(entry.cidr));
  }
}

bool cidrs_cover_address_family(const std::vector<std::string> &cidrs) {
  std::vector<std::string> canonical = cidrs;
  canonicalize_cidr_list(canonical);
  return parsed_cidrs_cover_family(canonical);
}

void canonicalize_port_ranges(std::vector<PortRange> &ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const PortRange &a, const PortRange &b) {
              return a.from != b.from ? a.from < b.from : a.to < b.to;
            });
  std::vector<PortRange> merged;
  merged.reserve(ranges.size());
  for (const auto &range : ranges) {
    if (!merged.empty() &&
        static_cast<unsigned>(range.from) <=
            static_cast<unsigned>(merged.back().to) + 1U) {
      merged.back().to = std::max(merged.back().to, range.to);
    } else {
      merged.push_back(range);
    }
  }
  ranges = std::move(merged);
}

void canonicalize_physical_rule(PhysicalRule &rule) {
  for (auto &match : rule.matches) {
    canonicalize_match(match);
  }
  // A positive address match covering its whole family (`0.0.0.0/0`, or the
  // halves nft merges into it) matches every packet of that family:
  // iptables-save omits it and nft keeps it, so the canonical form drops it
  // and keeps only the family it implied.
  for (auto it = rule.matches.begin(); it != rule.matches.end();) {
    const auto *addr = std::get_if<AddrMatch>(&*it);
    if (addr == nullptr || addr->negate ||
        !parsed_cidrs_cover_family(addr->cidrs)) {
      ++it;
      continue;
    }
    if (rule.family == FirewallFamily::any) {
      Cidr first;
      if (parse_cidr(addr->cidrs.front(), first)) {
        rule.family = first.v6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
      }
    }
    it = rule.matches.erase(it);
  }
  for (auto &statement : rule.statements) {
    if (auto *late = std::get_if<LateMatchStmt>(&statement)) {
      canonicalize_match(late->match);
    } else if (auto *vmap = std::get_if<VmapStmt>(&statement)) {
      std::sort(vmap->entries.begin(), vmap->entries.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
    }
  }
  // Implied protocol of tcp/udp port matches.
  std::vector<L4Proto> have;
  for (const auto &match : rule.matches) {
    if (const auto *proto = std::get_if<ProtoMatch>(&match)) {
      have.push_back(proto->proto);
    }
  }
  const std::size_t original = rule.matches.size();
  for (std::size_t i = 0; i < original; ++i) {
    const auto *port = std::get_if<PortMatch>(&rule.matches[i]);
    if (port == nullptr || port->transport == PhysicalTransport::any) continue;
    const L4Proto proto = port->transport == PhysicalTransport::tcp
                              ? L4Proto::Tcp
                              : L4Proto::Udp;
    if (std::find(have.begin(), have.end(), proto) == have.end()) {
      rule.matches.push_back(ProtoMatch{proto});
      have.push_back(proto);
    }
  }
  // Drop duplicate ProtoMatch entries (explicit `-p` plus implied).
  std::vector<PhysicalMatch> unique_matches;
  unique_matches.reserve(rule.matches.size());
  for (auto &match : rule.matches) {
    if (std::holds_alternative<ProtoMatch>(match)) {
      bool seen = false;
      for (const auto &kept : unique_matches) {
        if (kept == match) {
          seen = true;
          break;
        }
      }
      if (seen) continue;
    }
    unique_matches.push_back(std::move(match));
  }
  rule.matches = std::move(unique_matches);
  std::stable_sort(rule.matches.begin(), rule.matches.end(),
                   [](const PhysicalMatch &a, const PhysicalMatch &b) {
                     return a.index() < b.index();
                   });
}

// ===========================================================================
// Shared parsing helpers
// ===========================================================================

namespace {

constexpr std::string_view kIptablesOwnedPrefix = "KeenPbr";
constexpr std::string_view kNftTableName = "KeenPbrTable";

std::optional<uint32_t> parse_u32(std::string_view text) {
  if (text.empty()) return std::nullopt;
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  }
  uint64_t value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value > 0xFFFFFFFFULL) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(value);
}

std::optional<uint16_t> parse_port(std::string_view text) {
  const auto value = parse_u32(text);
  if (!value.has_value() || *value > 65535U) return std::nullopt;
  return static_cast<uint16_t>(*value);
}

// Jumps into these chains must be the first rule of their builtin chain.
bool is_pinned_hook_target(PhysicalChainRole role) {
  return role == PhysicalChainRole::iptables_dns_hold ||
         role == PhysicalChainRole::iptables_sniff;
}

std::size_t builtin_slot(const PhysicalChainId &id) {
  switch (id.role) {
  case PhysicalChainRole::system_prerouting:
    return 0;
  case PhysicalChainRole::system_output:
    return 3;
  default:
    break;
  }
  if (id.name == "INPUT") return 1;
  if (id.name == "FORWARD") return 2;
  return 4; // POSTROUTING
}

std::optional<FirewallRuleKey> key_from_comment(std::string_view comment) {
  try {
    return FirewallRuleKey::from_comment(comment);
  } catch (...) {
    // Malformed comments and unknown versions are not keys; the rule is still
    // owned by chain.
    return std::nullopt;
  }
}

PhysicalChain &chain_for(PhysicalRuleset &set, const PhysicalChainId &id) {
  for (auto &chain : set.chains) {
    if (chain.id == id) return chain;
  }
  set.chains.push_back(PhysicalChain{id, std::nullopt, {}});
  set.chains.back().rules.reserve(16);
  return set.chains.back();
}

bool is_owned_role(PhysicalChainRole role) {
  return role != PhysicalChainRole::system_prerouting &&
         role != PhysicalChainRole::system_output &&
         role != PhysicalChainRole::system_other;
}

PhysicalChainId iptables_id(PhysicalChainRole role, PhysicalTable table,
                            FirewallFamily family, std::string_view name) {
  PhysicalChainId id;
  id.role = role;
  id.table = table;
  id.family = family;
  id.name.assign(name);
  return id;
}

} // namespace

std::optional<PhysicalChainId> classify_iptables_chain(std::string_view name,
                                                       PhysicalTable table,
                                                       FirewallFamily family) {
  using R = PhysicalChainRole;
  if (name == "PREROUTING") {
    return iptables_id(R::system_prerouting, table, family, name);
  }
  if (name == "OUTPUT") {
    return iptables_id(R::system_output, table, family, name);
  }
  if (name == "INPUT" || name == "FORWARD" || name == "POSTROUTING") {
    return iptables_id(R::system_other, table, family, name);
  }
  if (name.substr(0, kIptablesOwnedPrefix.size()) != kIptablesOwnedPrefix) {
    return std::nullopt;
  }
  // Only the current layout is classified as ours-and-expected.  Leftovers of
  // the retired A/B layout (KeenPbrTable_A/B, KeenPbrRaw_A/B,
  // KeenPbrOutput_A/B, KeenPbrTable_OUTPUT) are other_owned so a stale chain is
  // reported.
  if (name == "KeenPbrTable" || name == "KeenPbrRaw") {
    return iptables_id(R::iptables_prerouting, table, family, name);
  }
  if (name == "KeenPbrOutput") {
    return iptables_id(R::iptables_output, table, family, name);
  }
  if (name == "KeenPbrDnsHold") {
    return iptables_id(R::iptables_dns_hold, table, family, name);
  }
  if (name == "KeenPbrSniff") {
    return iptables_id(R::iptables_sniff, table, family, name);
  }
  return iptables_id(R::other_owned, table, family, name);
}

// ===========================================================================
// iptables-save parser
// ===========================================================================

namespace {

// Split one line into whitespace separated tokens; a double-quoted token is
// returned without its quotes (escapes are left as written).
void tokenize(std::string_view line, std::vector<std::string_view> &out) {
  out.clear();
  std::size_t pos = 0;
  const std::size_t size = line.size();
  while (pos < size) {
    while (pos < size && (line[pos] == ' ' || line[pos] == '\t' ||
                          line[pos] == '\r')) {
      ++pos;
    }
    if (pos >= size) break;
    if (line[pos] == '"') {
      const std::size_t start = ++pos;
      while (pos < size && line[pos] != '"') {
        if (line[pos] == '\\' && pos + 1 < size) ++pos;
        ++pos;
      }
      out.push_back(line.substr(start, pos - start));
      if (pos < size) ++pos;
      continue;
    }
    const std::size_t start = pos;
    while (pos < size && line[pos] != ' ' && line[pos] != '\t' &&
           line[pos] != '\r') {
      ++pos;
    }
    out.push_back(line.substr(start, pos - start));
  }
}

std::string join_tokens(const std::vector<std::string_view> &tokens,
                        std::size_t from, std::size_t to) {
  std::string text;
  for (std::size_t i = from; i < to && i < tokens.size(); ++i) {
    if (!text.empty()) text.push_back(' ');
    text.append(tokens[i]);
  }
  return text;
}

bool is_iptables_option(std::string_view token) {
  return token.size() >= 2 && token[0] == '-' && token != "!" &&
         !(token[1] >= '0' && token[1] <= '9');
}

struct IptablesRuleParser {
  PhysicalTable table;
  FirewallFamily family;
  std::vector<std::string_view> tokens;

  struct Result {
    PhysicalRule rule;
    std::optional<std::string_view> comment;
    // connbytes needs --connbytes, --connbytes-dir and --connbytes-mode; the
    // kernel prints all three.  A partial one is not trusted.
    std::optional<std::size_t> connbytes_index;
    bool connbytes_dir{false};
    bool connbytes_mode{false};
  };

  void unknown_match(Result &r, std::size_t from, std::size_t to) const {
    r.rule.matches.push_back(UnknownMatch{join_tokens(tokens, from, to)});
  }

  // Parse `[!] --sport|--dport|--sports|--dports value` options of tcp, udp and
  // multiport.  Returns the index past the consumed tokens, or `at` when the
  // option is not a port option.
  std::size_t parse_port_option(Result &r, std::size_t at, bool negate,
                                PhysicalTransport transport) const {
    if (at + 1 >= tokens.size()) return at;
    const std::string_view option = tokens[at];
    PhysicalDir dir;
    if (option == "--sport" || option == "--sports") {
      dir = PhysicalDir::src;
    } else if (option == "--dport" || option == "--dports") {
      dir = PhysicalDir::dst;
    } else {
      return at;
    }
    PortMatch match;
    match.transport = transport;
    match.dir = dir;
    match.negate = negate;
    std::string_view list = tokens[at + 1];
    bool ok = true;
    while (!list.empty() && ok) {
      const auto comma = list.find(',');
      const std::string_view item = list.substr(0, comma);
      const auto colon = item.find(':');
      const auto from = parse_port(item.substr(0, colon));
      const auto to = colon == std::string_view::npos
                          ? from
                          : parse_port(item.substr(colon + 1));
      if (!from.has_value() || !to.has_value()) {
        ok = false;
        break;
      }
      match.ranges.push_back({*from, *to});
      list = comma == std::string_view::npos ? std::string_view{}
                                             : list.substr(comma + 1);
    }
    if (!ok || match.ranges.empty()) {
      unknown_match(r, negate ? at - 1 : at, at + 2);
    } else {
      r.rule.matches.push_back(std::move(match));
    }
    return at + 2;
  }

  Result parse(std::size_t first) const {
    Result r = parse_tokens(first);
    if (r.connbytes_index.has_value() &&
        !(r.connbytes_dir && r.connbytes_mode)) {
      auto &match = r.rule.matches[*r.connbytes_index];
      const auto &bytes = std::get<ConnbytesMatch>(match);
      match = UnknownMatch{"-m connbytes --connbytes " +
                           std::to_string(bytes.from) + ":" +
                           std::to_string(bytes.to) + " (incomplete)"};
    }
    return r;
  }

  Result parse_tokens(std::size_t first) const {
    Result r;
    r.rule.family = family;
    PhysicalTransport proto_transport = PhysicalTransport::any;
    std::string_view module;
    bool negate = false;
    std::size_t i = first;
    const std::size_t size = tokens.size();
    while (i < size) {
      const std::string_view token = tokens[i];
      if (token == "!") {
        negate = true;
        ++i;
        continue;
      }
      const bool neg = negate;
      negate = false;
      const std::size_t option_start = neg ? i - 1 : i;

      if (token == "-m" && i + 1 < size) {
        module = tokens[i + 1];
        static constexpr std::array<std::string_view, 10> kKnownModules{
            "tcp",  "udp",  "multiport", "comment", "set",
            "dscp", "mark", "connmark",  "conntrack", "connbytes"};
        if (std::find(kKnownModules.begin(), kKnownModules.end(), module) ==
            kKnownModules.end()) {
          // Never drop a module silently, even when none of its options is
          // recognised below.
          unknown_match(r, option_start, i + 2);
        }
        i += 2;
        continue;
      }
      if (token == "-j" || token == "-g") {
        parse_target(r, i);
        return r;
      }
      if (token == "-i" && i + 1 < size) {
        r.rule.matches.push_back(IifMatch{neg, {std::string(tokens[i + 1])}});
        i += 2;
        continue;
      }
      if ((token == "-s" || token == "-d") && i + 1 < size) {
        AddrMatch match;
        match.dir = token == "-s" ? PhysicalDir::src : PhysicalDir::dst;
        match.negate = neg;
        std::string_view list = tokens[i + 1];
        while (!list.empty()) {
          const auto comma = list.find(',');
          match.cidrs.emplace_back(list.substr(0, comma));
          list = comma == std::string_view::npos ? std::string_view{}
                                                 : list.substr(comma + 1);
        }
        r.rule.matches.push_back(std::move(match));
        i += 2;
        continue;
      }
      if (token == "-p" && i + 1 < size) {
        const std::string_view proto = tokens[i + 1];
        if (!neg && proto == "tcp") {
          r.rule.matches.push_back(ProtoMatch{L4Proto::Tcp});
          proto_transport = PhysicalTransport::tcp;
        } else if (!neg && proto == "udp") {
          r.rule.matches.push_back(ProtoMatch{L4Proto::Udp});
          proto_transport = PhysicalTransport::udp;
        } else {
          unknown_match(r, option_start, i + 2);
        }
        i += 2;
        continue;
      }

      // Module options.
      if (!module.empty() && is_iptables_option(token)) {
        const std::size_t consumed = parse_module_option(
            r, module, i, neg, proto_transport, option_start);
        if (consumed > i) {
          i = consumed;
          continue;
        }
      }

      // Anything else: keep the option and its arguments verbatim.
      std::size_t end = i + 1;
      while (end < size && !is_iptables_option(tokens[end]) &&
             tokens[end] != "!") {
        ++end;
      }
      unknown_match(r, option_start, end);
      i = end;
    }
    return r;
  }

  // Returns the index after the consumed option, or `at` if not handled.
  std::size_t parse_module_option(Result &r, std::string_view module,
                                  std::size_t at, bool neg,
                                  PhysicalTransport proto_transport,
                                  std::size_t option_start) const {
    const std::string_view option = tokens[at];
    const std::size_t size = tokens.size();
    if (module == "tcp" || module == "udp") {
      const auto transport =
          module == "tcp" ? PhysicalTransport::tcp : PhysicalTransport::udp;
      return parse_port_option(r, at, neg, transport);
    }
    if (module == "multiport") {
      if (proto_transport == PhysicalTransport::any) return at;
      return parse_port_option(r, at, neg, proto_transport);
    }
    if (module == "comment" && option == "--comment" && at + 1 < size) {
      r.comment = tokens[at + 1];
      return at + 2;
    }
    if (module == "set" && option == "--match-set" && at + 2 < size) {
      const std::string_view dir = tokens[at + 2];
      if (dir == "dst" || dir == "src") {
        r.rule.matches.push_back(
            SetMatch{std::string(tokens[at + 1]),
                     dir == "src" ? PhysicalDir::src : PhysicalDir::dst, neg});
        return at + 3;
      }
      return at;
    }
    if (module == "dscp" && option == "--dscp" && at + 1 < size && !neg) {
      const auto value = parse_u32(tokens[at + 1]);
      if (value.has_value() && *value <= 63U) {
        r.rule.matches.push_back(DscpMatch{static_cast<uint8_t>(*value)});
        return at + 2;
      }
      return at;
    }
    if ((module == "mark" || module == "connmark") && option == "--mark" &&
        at + 1 < size) {
      const std::string_view text = tokens[at + 1];
      const auto slash = text.find('/');
      const auto value = parse_u32(text.substr(0, slash));
      // iptables-save omits the mask when it is all ones ("! --mark 0x0").
      const auto mask = slash == std::string_view::npos
                            ? std::optional<uint32_t>{0xFFFFFFFFu}
                            : parse_u32(text.substr(slash + 1));
      if (value.has_value() && mask.has_value()) {
        MarkMatch match;
        match.kind = module == "mark" ? PhysicalMarkKind::packet
                                      : PhysicalMarkKind::conntrack;
        match.mask = *mask;
        match.negate = neg;
        match.values.push_back(*value);
        r.rule.matches.push_back(std::move(match));
        return at + 2;
      }
      return at;
    }
    if (module == "connbytes" && !neg) {
      if (option == "--connbytes" && at + 1 < size &&
          !r.connbytes_index.has_value()) {
        const std::string_view text = tokens[at + 1];
        const auto colon = text.find(':');
        const auto from = parse_u32(text.substr(0, colon));
        const auto to = colon == std::string_view::npos
                            ? std::nullopt
                            : parse_u32(text.substr(colon + 1));
        if (from.has_value() && to.has_value() && *from <= *to) {
          r.connbytes_index = r.rule.matches.size();
          r.rule.matches.push_back(ConnbytesMatch{
              ConnbytesDir::original, ConnbytesMode::packets, *from, *to});
          return at + 2;
        }
        return at;
      }
      if ((option == "--connbytes-dir" || option == "--connbytes-mode") &&
          at + 1 < size && r.connbytes_index.has_value()) {
        auto &bytes = std::get<ConnbytesMatch>(r.rule.matches[*r.connbytes_index]);
        const std::string_view value = tokens[at + 1];
        if (option == "--connbytes-dir" && !r.connbytes_dir) {
          if (value == "original") bytes.dir = ConnbytesDir::original;
          else if (value == "reply") bytes.dir = ConnbytesDir::reply;
          else if (value == "both") bytes.dir = ConnbytesDir::both;
          else return at;
          r.connbytes_dir = true;
          return at + 2;
        }
        if (option == "--connbytes-mode" && !r.connbytes_mode) {
          if (value == "packets") bytes.mode = ConnbytesMode::packets;
          else if (value == "bytes") bytes.mode = ConnbytesMode::bytes;
          else return at;
          r.connbytes_mode = true;
          return at + 2;
        }
      }
      return at;
    }
    if (module == "conntrack") {
      if (option == "--ctdir" && at + 1 < size && !neg &&
          (tokens[at + 1] == "ORIGINAL" || tokens[at + 1] == "REPLY")) {
        r.rule.matches.push_back(CtDirMatch{tokens[at + 1] == "ORIGINAL"});
        return at + 2;
      }
      if (option == "--ctstate" && at + 1 < size) {
        CtStateMatch match;
        match.negate = neg;
        std::string_view list = tokens[at + 1];
        bool ok = true;
        while (!list.empty()) {
          const auto comma = list.find(',');
          const std::string_view name = list.substr(0, comma);
          if (name == "NEW") match.states |= ct_new;
          else if (name == "ESTABLISHED") match.states |= ct_established;
          else if (name == "RELATED") match.states |= ct_related;
          else if (name == "INVALID") match.states |= ct_invalid;
          else if (name == "UNTRACKED") match.states |= ct_untracked;
          else if (name == "SNAT") match.states |= ct_snat;
          else if (name == "DNAT") match.states |= ct_dnat;
          else ok = false;
          list = comma == std::string_view::npos ? std::string_view{}
                                                 : list.substr(comma + 1);
        }
        if (ok && match.states != 0) {
          r.rule.matches.push_back(match);
          return at + 2;
        }
        return at;
      }
    }
    (void)option_start;
    return at;
  }

  void parse_target(Result &r, std::size_t at) const {
    const bool is_goto = tokens[at] == "-g";
    const std::size_t size = tokens.size();
    if (at + 1 >= size) {
      r.rule.statements.push_back(UnknownStmt{join_tokens(tokens, at, size)});
      return;
    }
    const std::string_view target = tokens[at + 1];
    const std::size_t args = at + 2;
    if (!is_goto && args >= size) {
      if (target == "ACCEPT") {
        r.rule.statements.push_back(VerdictStmt{PhysicalVerdict::accept});
        return;
      }
      if (target == "DROP") {
        r.rule.statements.push_back(VerdictStmt{PhysicalVerdict::drop});
        return;
      }
      if (target == "RETURN") {
        r.rule.statements.push_back(VerdictStmt{PhysicalVerdict::return_});
        return;
      }
    }
    if (!is_goto && target == "NFQUEUE") {
      QueueStmt queue;
      bool ok = false;
      for (std::size_t i = args; i < size;) {
        if (tokens[i] == "--queue-num" && i + 1 < size) {
          const auto value = parse_u32(tokens[i + 1]);
          if (!value.has_value() || *value > 65535U) {
            ok = false;
            break;
          }
          queue.num = static_cast<uint16_t>(*value);
          ok = true;
          i += 2;
        } else if (tokens[i] == "--queue-bypass") {
          queue.bypass = true;
          ++i;
        } else {
          ok = false;
          break;
        }
      }
      if (ok) {
        r.rule.statements.push_back(queue);
        return;
      }
    }
    if (!is_goto && target == "NFLOG") {
      // iptables-save prints a bare `-j NFLOG` for group 0.
      LogStmt log;
      bool ok = true;
      for (std::size_t i = args; i < size;) {
        if (tokens[i] == "--nflog-group" && i + 1 < size) {
          const auto value = parse_u32(tokens[i + 1]);
          if (!value.has_value() || *value > 65535U) {
            ok = false;
            break;
          }
          log.group = static_cast<uint16_t>(*value);
          i += 2;
        } else if (tokens[i] == "--nflog-size" && i + 1 < size) {
          const auto value = parse_u32(tokens[i + 1]);
          if (!value.has_value() || *value > 65535U) {
            ok = false;
            break;
          }
          log.snaplen = static_cast<uint16_t>(*value);
          i += 2;
        } else if (tokens[i] == "--nflog-threshold" && i + 1 < size) {
          const auto value = parse_u32(tokens[i + 1]);
          if (!value.has_value() || *value == 0) {
            ok = false;
            break;
          }
          log.threshold = *value;
          i += 2;
        } else {
          ok = false;
          break;
        }
      }
      if (ok) {
        r.rule.statements.push_back(log);
        return;
      }
    }
    if (!is_goto && target == "MARK" && size == args + 2 &&
        tokens[args] == "--set-xmark") {
      // MARK --set-xmark value/mask (iptables-save prints this for every
      // MARK revision >= 1, including rules added with --set-mark).
      const std::string_view text = tokens[args + 1];
      const auto slash = text.find('/');
      const auto value = parse_u32(text.substr(0, slash));
      const auto mask = slash == std::string_view::npos
                            ? std::optional<uint32_t>{0xFFFFFFFFu}
                            : parse_u32(text.substr(slash + 1));
      // (mark & ~mask) ^ value equals "| value" only when value is inside
      // the mask; anything else is not representable.
      if (value.has_value() && mask.has_value() && (*value & ~*mask) == 0) {
        r.rule.statements.push_back(
            SetMarkStmt{PhysicalMarkKind::packet, *value, *mask});
        return;
      }
    }
    if (!is_goto && target == "CONNMARK" && args < size &&
        (tokens[args] == "--save-mark" || tokens[args] == "--restore-mark") &&
        size == args + 5 && tokens[args + 1] == "--nfmask" &&
        tokens[args + 3] == "--ctmask") {
      // iptables-save always spells --mask as --nfmask X --ctmask X.
      const auto nfmask = parse_u32(tokens[args + 2]);
      const auto ctmask = parse_u32(tokens[args + 4]);
      if (nfmask.has_value() && ctmask.has_value()) {
        r.rule.statements.push_back(
            CopyMarkStmt{tokens[args] == "--save-mark", *nfmask, *ctmask});
        return;
      }
    }
    if (args >= size) {
      const auto id = classify_iptables_chain(target, table, family);
      if (id.has_value() && is_owned_role(id->role)) {
        r.rule.statements.push_back(JumpStmt{*id, is_goto});
        return;
      }
    }
    r.rule.statements.push_back(UnknownStmt{join_tokens(tokens, at, size)});
  }
};

} // namespace

PhysicalRuleset parse_iptables_save(std::string_view output,
                                    FirewallFamily family,
                                    PhysicalTable default_table) {
  PhysicalRuleset result;
  PhysicalTable table = default_table;
  bool table_active = true;
  IptablesRuleParser parser{table, family, {}};
  parser.tokens.reserve(48);
  // Rules seen so far in each builtin chain of the current table (ours and
  // foreign), indexed by builtin_slot().
  std::array<uint32_t, 5> builtin_rules{};

  std::size_t pos = 0;
  while (pos < output.size()) {
    std::size_t end = output.find('\n', pos);
    if (end == std::string_view::npos) end = output.size();
    std::string_view line = output.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty() || line[0] == '#') continue;

    if (line[0] == '*') {
      const std::string_view name = line.substr(1);
      table_active = true;
      builtin_rules.fill(0);
      if (name == "mangle") table = PhysicalTable::mangle;
      else if (name == "raw") table = PhysicalTable::raw;
      else table_active = false;
      continue;
    }
    if (line == "COMMIT") {
      continue;
    }
    if (!table_active) continue;

    // `-c` counters prefix: "[pkts:bytes] -A ...".
    if (line[0] == '[') {
      const auto close = line.find(']');
      if (close == std::string_view::npos) continue;
      line.remove_prefix(close + 1);
      while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    }

    if (line[0] == ':') {
      // ":CHAIN POLICY [pkts:bytes]"
      const auto space = line.find(' ');
      const auto name = line.substr(1, space == std::string_view::npos
                                           ? std::string_view::npos
                                           : space - 1);
      const auto id = classify_iptables_chain(name, table, family);
      if (id.has_value() && is_owned_role(id->role)) {
        chain_for(result, *id);
      }
      continue;
    }

    parser.table = table;
    tokenize(line, parser.tokens);
    if (parser.tokens.size() < 2) continue;
    const std::string_view command = parser.tokens[0];
    if (command == "-N") {
      const auto id = classify_iptables_chain(parser.tokens[1], table, family);
      if (id.has_value() && is_owned_role(id->role)) {
        chain_for(result, *id);
      }
      continue;
    }
    if (command != "-A") continue; // -P and anything else carries no rule

    const auto chain_id =
        classify_iptables_chain(parser.tokens[1], table, family);
    if (!chain_id.has_value()) continue; // foreign chain
    auto parsed = parser.parse(2);
    if (!is_owned_role(chain_id->role)) {
      // System chain: keep only hook rules into keen-pbr chains, but count
      // every rule so the position of a pinned jump is its real index.
      const uint32_t position = builtin_rules[builtin_slot(*chain_id)]++;
      bool hook = false;
      bool pinned = false;
      for (const auto &statement : parsed.rule.statements) {
        if (const auto *jump = std::get_if<JumpStmt>(&statement)) {
          hook = true;
          pinned = pinned || is_pinned_hook_target(jump->target.role);
        }
      }
      if (!hook) continue;
      if (pinned) parsed.rule.hook_position = position;
    }
    if (parsed.comment.has_value()) {
      parsed.rule.key = key_from_comment(*parsed.comment);
    }
    canonicalize_physical_rule(parsed.rule);
    chain_for(result, *chain_id).rules.push_back(std::move(parsed.rule));
  }
  return result;
}

// ===========================================================================
// nft JSON parser
// ===========================================================================

namespace {

using json = nlohmann::json;

std::optional<uint8_t> nft_dscp_by_name(std::string_view name) {
  struct Entry {
    std::string_view name;
    uint8_t value;
  };
  // libnftables' dscp symbol table (`nft describe ip dscp`).
  static constexpr std::array<Entry, 25> kNames{{
      {"cs0", 0},   {"cs1", 8},   {"cs2", 16},  {"cs3", 24},  {"cs4", 32},
      {"cs5", 40},  {"cs6", 48},  {"cs7", 56},  {"df", 0},    {"be", 0},
      {"lephb", 1}, {"af11", 10}, {"af12", 12}, {"af13", 14}, {"af21", 18},
      {"af22", 20}, {"af23", 22}, {"af31", 26}, {"af32", 28}, {"af33", 30},
      {"af41", 34}, {"af42", 36}, {"af43", 38}, {"va", 44},   {"ef", 46},
  }};
  for (const auto &entry : kNames) {
    if (entry.name == name) return entry.value;
  }
  return std::nullopt;
}

std::optional<uint32_t> json_u32(const json &value) {
  if (value.is_number_unsigned()) {
    const auto v = value.get<uint64_t>();
    if (v <= 0xFFFFFFFFULL) return static_cast<uint32_t>(v);
  } else if (value.is_number_integer()) {
    const auto v = value.get<int64_t>();
    if (v >= 0 && v <= 0xFFFFFFFFLL) return static_cast<uint32_t>(v);
  }
  return std::nullopt;
}

std::optional<uint64_t> json_u64(const json &value) {
  if (value.is_number_unsigned()) return value.get<uint64_t>();
  if (value.is_number_integer()) {
    const auto v = value.get<int64_t>();
    if (v >= 0) return static_cast<uint64_t>(v);
  }
  return std::nullopt;
}

PhysicalChainId nft_chain_id(std::string_view name) {
  PhysicalChainId id;
  id.table = PhysicalTable::nft_inet;
  id.family = FirewallFamily::any;
  id.name.assign(name);
  if (name == "prerouting") {
    id.role = PhysicalChainRole::nft_prerouting;
  } else if (name == "output") {
    id.role = PhysicalChainRole::nft_output;
  } else if (name == "dns_hold") {
    id.role = PhysicalChainRole::nft_dns_hold;
  } else if (name == "sniff_fwd") {
    id.role = PhysicalChainRole::nft_sniff_forward;
  } else if (name == "sniff_out") {
    id.role = PhysicalChainRole::nft_sniff_output;
  } else if (name.size() == 16 && name.substr(0, 8) == "setmark_") {
    const std::string_view hex = name.substr(8);
    uint32_t mark = 0;
    const auto result =
        std::from_chars(hex.data(), hex.data() + hex.size(), mark, 16);
    if (result.ec == std::errc{} && result.ptr == hex.data() + hex.size()) {
      id.role = PhysicalChainRole::nft_setter;
      id.setter_mark = mark;
    } else {
      id.role = PhysicalChainRole::other_owned;
    }
  } else {
    id.role = PhysicalChainRole::other_owned;
  }
  return id;
}

struct NftRuleParser {
  PhysicalRule rule;
  bool family_conflict{false};
  bool statement_seen{false};

  void note_family(FirewallFamily family) {
    if (rule.family == FirewallFamily::any) {
      rule.family = family;
    } else if (rule.family != family) {
      family_conflict = true;
    }
  }

  void add_match(PhysicalMatch match) {
    if (statement_seen) {
      rule.statements.push_back(LateMatchStmt{std::move(match)});
    } else {
      rule.matches.push_back(std::move(match));
    }
  }

  void add_unknown_match(const json &expr) {
    add_match(UnknownMatch{expr.dump()});
  }

  void add_statement(PhysicalStatement statement) {
    statement_seen = true;
    rule.statements.push_back(std::move(statement));
  }

  // Parse one address operand into canonical-able CIDR text.
  static bool address_text(const json &value, std::vector<std::string> &out) {
    if (value.is_string()) {
      out.push_back(value.get<std::string>());
      return true;
    }
    if (value.is_object() && value.size() == 1 && value.contains("prefix")) {
      const auto &prefix = value["prefix"];
      if (prefix.is_object() && prefix.contains("addr") &&
          prefix["addr"].is_string() && prefix.contains("len") &&
          prefix["len"].is_number_integer()) {
        out.push_back(prefix["addr"].get<std::string>() + "/" +
                      std::to_string(prefix["len"].get<int>()));
        return true;
      }
    }
    return false;
  }

  static bool ports_from(const json &value, std::vector<PortRange> &out) {
    const auto one = [&out](const json &item) {
      if (item.is_number_integer()) {
        const auto v = json_u32(item);
        if (!v.has_value() || *v > 65535U) return false;
        out.push_back({static_cast<uint16_t>(*v), static_cast<uint16_t>(*v)});
        return true;
      }
      if (item.is_object() && item.contains("range") &&
          item["range"].is_array() && item["range"].size() == 2) {
        const auto a = json_u32(item["range"][0]);
        const auto b = json_u32(item["range"][1]);
        if (!a.has_value() || !b.has_value() || *a > 65535U || *b > 65535U) {
          return false;
        }
        out.push_back({static_cast<uint16_t>(*a), static_cast<uint16_t>(*b)});
        return true;
      }
      return false;
    };
    if (value.is_object() && value.contains("set") && value["set"].is_array()) {
      for (const auto &item : value["set"]) {
        if (!one(item)) return false;
      }
      return !out.empty();
    }
    return one(value);
  }

  // `left` is a mark-ish operand: meta mark / ct mark, optionally "& mask".
  static bool mark_operand(const json &left, PhysicalMarkKind &kind,
                           uint32_t &mask) {
    const json *base = &left;
    mask = 0xFFFFFFFFu;
    if (left.is_object() && left.size() == 1 && left.contains("&") &&
        left["&"].is_array() && left["&"].size() == 2) {
      base = &left["&"][0];
      const auto m = json_u32(left["&"][1]);
      if (!m.has_value()) return false;
      mask = *m;
    }
    if (base->is_object() && base->size() == 1) {
      if (base->contains("meta") && (*base)["meta"].value("key", "") == "mark") {
        kind = PhysicalMarkKind::packet;
        return true;
      }
      if (base->contains("ct") && (*base)["ct"].value("key", "") == "mark") {
        kind = PhysicalMarkKind::conntrack;
        return true;
      }
    }
    return false;
  }

  void parse_match(const json &expr) {
    const json &m = expr["match"];
    if (!m.is_object() || !m.contains("op") || !m["op"].is_string() ||
        !m.contains("left") || !m.contains("right")) {
      add_unknown_match(expr);
      return;
    }
    const std::string op = m["op"].get<std::string>();
    bool negate;
    if (op == "==" || op == "in") negate = false;
    else if (op == "!=") negate = true;
    else {
      add_unknown_match(expr);
      return;
    }
    const json &left = m["left"];
    const json &right = m["right"];

    PhysicalMarkKind mark_kind;
    uint32_t mark_mask;
    if (mark_operand(left, mark_kind, mark_mask)) {
      MarkMatch match;
      match.kind = mark_kind;
      match.mask = mark_mask;
      match.negate = negate;
      if (right.is_object() && right.contains("set") &&
          right["set"].is_array()) {
        for (const auto &item : right["set"]) {
          const auto v = json_u32(item);
          if (!v.has_value()) {
            add_unknown_match(expr);
            return;
          }
          match.values.push_back(*v);
        }
      } else {
        const auto v = json_u32(right);
        if (!v.has_value()) {
          add_unknown_match(expr);
          return;
        }
        match.values.push_back(*v);
      }
      add_match(std::move(match));
      return;
    }

    if (left.is_object() && left.size() == 1 && left.contains("meta")) {
      const std::string key = left["meta"].value("key", "");
      if (key == "nfproto" && !negate && right.is_string()) {
        const std::string value = right.get<std::string>();
        if (value == "ipv4" || value == "ipv6") {
          note_family(value == "ipv4" ? FirewallFamily::ipv4
                                      : FirewallFamily::ipv6);
          return;
        }
      } else if (key == "l4proto" && !negate && right.is_string()) {
        const std::string value = right.get<std::string>();
        if (value == "tcp" || value == "udp") {
          add_match(ProtoMatch{value == "tcp" ? L4Proto::Tcp : L4Proto::Udp});
          return;
        }
      } else if (key == "iifname") {
        IifMatch match;
        match.negate = negate;
        if (right.is_string()) {
          match.names.push_back(right.get<std::string>());
        } else if (right.is_object() && right.contains("set") &&
                   right["set"].is_array()) {
          for (const auto &item : right["set"]) {
            if (!item.is_string()) {
              add_unknown_match(expr);
              return;
            }
            match.names.push_back(item.get<std::string>());
          }
        } else {
          add_unknown_match(expr);
          return;
        }
        add_match(std::move(match));
        return;
      }
      add_unknown_match(expr);
      return;
    }

    if (left.is_object() && left.size() == 1 && left.contains("ct")) {
      const std::string key = left["ct"].value("key", "");
      if ((key == "packets" || key == "bytes") && !negate && op == "==") {
        // `ct [original|reply] packets|bytes A-B` prints as one `==` against a
        // range (a single count as a plain number); no `dir` means both
        // directions.  Any other ct operand attribute or comparison stays
        // unknown.
        const auto &ct = left["ct"];
        ConnbytesDir dir = ConnbytesDir::both;
        bool ok = ct.is_object();
        for (auto it = ct.begin(); ok && it != ct.end(); ++it) {
          if (it.key() == "key") continue;
          if (it.key() == "dir" && it.value().is_string() &&
              (it.value() == "original" || it.value() == "reply")) {
            dir = it.value() == "original" ? ConnbytesDir::original
                                           : ConnbytesDir::reply;
            continue;
          }
          ok = false;
        }
        std::optional<uint64_t> from;
        std::optional<uint64_t> to;
        if (right.is_object() && right.size() == 1 && right.contains("range") &&
            right["range"].is_array() && right["range"].size() == 2) {
          from = json_u64(right["range"][0]);
          to = json_u64(right["range"][1]);
        } else {
          from = to = json_u64(right);
        }
        if (ok && from.has_value() && to.has_value() && *from <= *to) {
          add_match(ConnbytesMatch{dir,
                                   key == "bytes" ? ConnbytesMode::bytes
                                                  : ConnbytesMode::packets,
                                   *from, *to});
          return;
        }
      } else if (key == "direction" && !negate && right.is_string()) {
        const std::string value = right.get<std::string>();
        if (value == "original" || value == "reply") {
          add_match(CtDirMatch{value == "original"});
          return;
        }
      } else if (key == "status" || key == "state") {
        CtStateMatch match;
        match.negate = negate;
        bool ok = true;
        const auto add = [&](const json &item) {
          if (!item.is_string()) {
            ok = false;
            return;
          }
          const std::string name = item.get<std::string>();
          if (key == "state" && name == "new") match.states |= ct_new;
          else if (key == "state" && name == "established") match.states |= ct_established;
          else if (key == "state" && name == "related") match.states |= ct_related;
          else if (key == "state" && name == "invalid") match.states |= ct_invalid;
          else if (key == "state" && name == "untracked") match.states |= ct_untracked;
          else if (key == "status" && name == "snat") match.states |= ct_snat;
          else if (key == "status" && name == "dnat") match.states |= ct_dnat;
          else ok = false;
        };
        if (right.is_object() && right.contains("set") &&
            right["set"].is_array()) {
          for (const auto &item : right["set"]) add(item);
        } else {
          add(right);
        }
        if (ok && match.states != 0) {
          add_match(match);
          return;
        }
      }
      add_unknown_match(expr);
      return;
    }

    if (left.is_object() && left.size() == 1 && left.contains("payload")) {
      const auto &payload = left["payload"];
      const std::string protocol = payload.value("protocol", "");
      const std::string field = payload.value("field", "");
      if (protocol == "ip" || protocol == "ip6") {
        const FirewallFamily family =
            protocol == "ip" ? FirewallFamily::ipv4 : FirewallFamily::ipv6;
        if (field == "daddr" || field == "saddr") {
          const PhysicalDir dir =
              field == "saddr" ? PhysicalDir::src : PhysicalDir::dst;
          if (right.is_string() && !right.get<std::string>().empty() &&
              right.get<std::string>()[0] == '@') {
            note_family(family);
            add_match(SetMatch{right.get<std::string>().substr(1), dir, negate});
            return;
          }
          AddrMatch match;
          match.dir = dir;
          match.negate = negate;
          bool ok;
          if (right.is_object() && right.contains("set") &&
              right["set"].is_array()) {
            ok = !right["set"].empty();
            for (const auto &item : right["set"]) {
              ok = ok && address_text(item, match.cidrs);
            }
          } else {
            ok = address_text(right, match.cidrs);
          }
          if (ok) {
            note_family(family);
            add_match(std::move(match));
            return;
          }
        } else if (field == "dscp" && !negate) {
          std::optional<uint32_t> value;
          if (right.is_string()) {
            const auto named = nft_dscp_by_name(right.get<std::string>());
            if (named.has_value()) value = *named;
          } else {
            value = json_u32(right);
          }
          if (value.has_value() && *value <= 63U) {
            note_family(family);
            add_match(DscpMatch{static_cast<uint8_t>(*value)});
            return;
          }
        }
      } else if ((protocol == "tcp" || protocol == "udp" || protocol == "th") &&
                 (field == "sport" || field == "dport")) {
        PortMatch match;
        match.transport = protocol == "tcp"   ? PhysicalTransport::tcp
                          : protocol == "udp" ? PhysicalTransport::udp
                                              : PhysicalTransport::any;
        match.dir = field == "sport" ? PhysicalDir::src : PhysicalDir::dst;
        match.negate = negate;
        if (ports_from(right, match.ranges)) {
          add_match(std::move(match));
          return;
        }
      }
    }
    add_unknown_match(expr);
  }

  void parse_vmap(const json &expr) {
    const json &v = expr["vmap"];
    VmapStmt stmt;
    bool ok = v.is_object() && v.contains("key") && v.contains("data");
    if (ok) {
      const json &key = v["key"];
      PhysicalMarkKind kind;
      uint32_t mask;
      if (key.is_object() && key.size() == 1 && key.contains("numgen")) {
        const auto &numgen = key["numgen"];
        const auto mod = numgen.contains("mod") ? json_u32(numgen["mod"])
                                                : std::nullopt;
        // `offset` is always printed (0 for the emitted rules).
        const auto offset =
            numgen.contains("offset") ? json_u32(numgen["offset"])
                                      : std::optional<uint32_t>{0};
        ok = numgen.value("mode", "") == "inc" && mod.has_value() &&
             offset.has_value() && *offset == 0;
        if (ok) {
          stmt.key = PhysicalVmapKey::numgen_inc;
          stmt.param = *mod;
        }
      } else if (mark_operand(key, kind, mask) &&
                 kind == PhysicalMarkKind::conntrack) {
        stmt.key = PhysicalVmapKey::conntrack_mark_and;
        stmt.param = mask;
      } else {
        ok = false;
      }
    }
    if (ok) {
      const json &data = v["data"];
      ok = data.is_object() && data.contains("set") && data["set"].is_array();
      if (ok) {
        for (const auto &entry : data["set"]) {
          if (!entry.is_array() || entry.size() != 2 ||
              !json_u32(entry[0]).has_value() || !entry[1].is_object() ||
              !entry[1].contains("jump") || !entry[1]["jump"].is_object() ||
              !entry[1]["jump"].contains("target") ||
              !entry[1]["jump"]["target"].is_string()) {
            ok = false;
            break;
          }
          stmt.entries.emplace_back(
              *json_u32(entry[0]),
              nft_chain_id(entry[1]["jump"]["target"].get<std::string>()));
        }
      }
    }
    if (ok) {
      add_statement(std::move(stmt));
    } else {
      add_statement(UnknownStmt{expr.dump()});
    }
  }

  void parse_mangle(const json &expr) {
    const json &m = expr["mangle"];
    PhysicalMarkKind kind;
    uint32_t unused;
    if (m.is_object() && m.contains("key") && m.contains("value") &&
        mark_operand(m["key"], kind, unused) &&
        !(m["key"].contains("&"))) {
      const json &value = m["value"];
      if (const auto plain = json_u32(value)) {
        add_statement(SetMarkStmt{kind, *plain, 0xFFFFFFFFu});
        return;
      }
      // (mark & A) | V  ==  clear (~A | V) bits, then set V.  libnftables
      // rewrites the emitted (mark & ~mask) | value into
      // (mark & ~(mask & ~value)) | value, so the mask is recovered as
      // ~A | V.
      if (value.is_object() && value.size() == 1 && value.contains("|") &&
          value["|"].is_array() && value["|"].size() == 2) {
        const json &and_part = value["|"][0];
        const auto set_bits = json_u32(value["|"][1]);
        PhysicalMarkKind inner_kind;
        uint32_t keep;
        if (set_bits.has_value() && mark_operand(and_part, inner_kind, keep) &&
            inner_kind == kind && and_part.is_object() &&
            and_part.contains("&")) {
          add_statement(SetMarkStmt{kind, *set_bits, ~keep | *set_bits});
          return;
        }
      }
    }
    add_statement(UnknownStmt{expr.dump()});
  }

  void parse_expr(const json &expr) {
    if (!expr.is_object() || expr.size() != 1) {
      add_statement(UnknownStmt{expr.dump()});
      return;
    }
    const std::string &name = expr.begin().key();
    if (name == "match") {
      parse_match(expr);
    } else if (name == "counter") {
      // Counters are not part of the physical rule.
    } else if (name == "accept" || name == "drop" || name == "return") {
      add_statement(VerdictStmt{name == "accept" ? PhysicalVerdict::accept
                                : name == "drop" ? PhysicalVerdict::drop
                                                 : PhysicalVerdict::return_});
    } else if (name == "jump" || name == "goto") {
      const json &body = expr[name];
      if (body.is_object() && body.contains("target") &&
          body["target"].is_string()) {
        add_statement(JumpStmt{nft_chain_id(body["target"].get<std::string>()),
                               name == "goto"});
      } else {
        add_statement(UnknownStmt{expr.dump()});
      }
    } else if (name == "vmap") {
      parse_vmap(expr);
    } else if (name == "mangle") {
      parse_mangle(expr);
    } else if (name == "queue") {
      // Only `queue num N [bypass]` is understood; a range (load balancing),
      // fanout or any other attribute stays unknown.
      const auto &q = expr["queue"];
      bool ok = q.is_object() && q.contains("num");
      std::optional<uint32_t> num;
      bool bypass = false;
      for (auto it = ok ? q.begin() : q.end(); ok && it != q.end(); ++it) {
        if (it.key() == "num") {
          num = json_u32(it.value());
          ok = num.has_value() && *num <= 65535U;
        } else if (it.key() == "flags" && it.value().is_array() &&
                   it.value().size() == 1 && it.value()[0].is_string() &&
                   it.value()[0] == "bypass") {
          bypass = true;
        } else {
          ok = false;
        }
      }
      if (ok) {
        add_statement(QueueStmt{static_cast<uint16_t>(*num), bypass});
      } else {
        add_statement(UnknownStmt{expr.dump()});
      }
    } else if (name == "log") {
      // Only the NFLOG form: `log group G [snaplen S] [queue-threshold T]`.
      // A syslog log (no group), prefix, level or flags stay unknown.
      const auto &l = expr["log"];
      bool ok = l.is_object() && l.contains("group");
      std::optional<uint32_t> group;
      uint32_t snaplen = 0;
      uint32_t threshold = 1;
      for (auto it = ok ? l.begin() : l.end(); ok && it != l.end(); ++it) {
        const auto value = json_u32(it.value());
        if (it.key() == "group") {
          group = value;
          ok = value.has_value() && *value <= 65535U;
        } else if (it.key() == "snaplen") {
          ok = value.has_value() && *value <= 65535U;
          if (ok) snaplen = *value;
        } else if (it.key() == "queue-threshold") {
          ok = value.has_value() && *value > 0;
          if (ok) threshold = *value;
        } else {
          ok = false;
        }
      }
      if (ok) {
        add_statement(LogStmt{static_cast<uint16_t>(*group),
                              static_cast<uint16_t>(snaplen), threshold});
      } else {
        add_statement(UnknownStmt{expr.dump()});
      }
    } else {
      add_statement(UnknownStmt{expr.dump()});
    }
  }
};

} // namespace

PhysicalRuleset parse_nft_json(std::string_view text) {
  json document;
  try {
    // Set element lists can hold the whole contents of a large list; they are
    // not part of the physical ruleset, so the parser never materializes them.
    document = json::parse(
        text.begin(), text.end(),
        [](int, json::parse_event_t event, json &parsed) {
          return !(event == json::parse_event_t::key && parsed == "elem");
        });
  } catch (const json::exception &error) {
    throw FirewallError(std::string("failed to parse nftables JSON: ") +
                        error.what());
  }
  if (!document.is_object() || !document.contains("nftables") ||
      !document["nftables"].is_array()) {
    throw FirewallError("failed to parse nftables JSON: no nftables array");
  }

  PhysicalRuleset result;
  for (const auto &item : document["nftables"]) {
    if (!item.is_object()) continue;
    if (item.contains("chain")) {
      const auto &c = item["chain"];
      if (!c.is_object() || c.value("family", "") != "inet" ||
          c.value("table", "") != kNftTableName || !c.contains("name") ||
          !c["name"].is_string()) {
        continue;
      }
      auto &chain = chain_for(result, nft_chain_id(c["name"].get<std::string>()));
      if (c.contains("hook")) {
        PhysicalBaseChain base;
        const std::string type = c.value("type", "");
        base.type = type == "filter"  ? PhysicalBaseChain::Type::filter
                    : type == "route" ? PhysicalBaseChain::Type::route
                                      : PhysicalBaseChain::Type::other;
        const std::string hook = c.value("hook", "");
        base.hook = hook == "prerouting" ? PhysicalBaseChain::Hook::prerouting
                    : hook == "output"   ? PhysicalBaseChain::Hook::output
                    : hook == "forward" ? PhysicalBaseChain::Hook::forward
                    : hook == "postrouting" ? PhysicalBaseChain::Hook::postrouting
                                         : PhysicalBaseChain::Hook::other;
        base.priority = static_cast<int32_t>(c.value("prio", 0));
        base.policy_accept = c.value("policy", "accept") == "accept";
        chain.base = base;
      }
    } else if (item.contains("rule")) {
      const auto &r = item["rule"];
      if (!r.is_object() || r.value("family", "") != "inet" ||
          r.value("table", "") != kNftTableName || !r.contains("chain") ||
          !r["chain"].is_string()) {
        continue;
      }
      NftRuleParser parser;
      if (r.contains("expr") && r["expr"].is_array()) {
        for (const auto &expr : r["expr"]) parser.parse_expr(expr);
      }
      if (parser.family_conflict) {
        parser.rule.matches.push_back(UnknownMatch{"conflicting L3 families"});
      }
      if (r.contains("comment") && r["comment"].is_string()) {
        parser.rule.key = key_from_comment(r["comment"].get<std::string>());
      }
      canonicalize_physical_rule(parser.rule);
      chain_for(result, nft_chain_id(r["chain"].get<std::string>()))
          .rules.push_back(std::move(parser.rule));
    }
  }
  return result;
}

} // namespace keen_pbr3
