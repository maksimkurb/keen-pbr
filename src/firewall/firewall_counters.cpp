#include "firewall_counters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace keen_pbr3 {
namespace {

constexpr std::string_view kSkipMarkedComment =
    "kpbr:v1:prefilter.skip_marked_packets:";

// Whitespace tokenizer that keeps a "double quoted" argument (iptables-save
// always quotes --comment) as one token without the quotes.
std::vector<std::string> tokenize(std::string_view line) {
  std::vector<std::string> tokens;
  std::size_t i = 0;
  while (i < line.size()) {
    if (line[i] == ' ' || line[i] == '\t') {
      ++i;
      continue;
    }
    std::string token;
    if (line[i] == '"') {
      ++i;
      while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1U < line.size()) ++i;
        token.push_back(line[i++]);
      }
      if (i < line.size()) ++i;
    } else {
      while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
        token.push_back(line[i++]);
      }
    }
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::optional<uint64_t> parse_u64(const std::string& text, int base) {
  if (text.empty()) return std::nullopt;
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text.c_str(), &end, base);
  if (end == text.c_str() || *end != '\0') return std::nullopt;
  return static_cast<uint64_t>(value);
}

std::optional<uint32_t> parse_mark_value(const std::string& text) {
  const auto slash = text.find('/');
  const auto value = parse_u64(text.substr(0, slash), 0);
  if (!value.has_value() || *value > 0xFFFFFFFFULL) return std::nullopt;
  return static_cast<uint32_t>(*value);
}

bool starts_with(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         std::string_view(text).substr(0, prefix.size()) == prefix;
}

std::string mark_label(uint32_t mark) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "mark_0x%x", mark);
  return buffer;
}

} // namespace

std::vector<IptablesCounterRule>
parse_iptables_save_counters(std::string_view text) {
  std::vector<IptablesCounterRule> rules;
  std::string table;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1U;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '*') {
      table.assign(line.substr(1));
      continue;
    }

    uint64_t packets = 0;
    uint64_t bytes = 0;
    if (line[0] == '[') {
      const auto close = line.find(']');
      const auto colon = line.find(':');
      if (close == std::string_view::npos || colon == std::string_view::npos ||
          colon > close) {
        continue;
      }
      const auto p = parse_u64(std::string(line.substr(1, colon - 1U)), 10);
      const auto b =
          parse_u64(std::string(line.substr(colon + 1U, close - colon - 1U)), 10);
      if (!p.has_value() || !b.has_value()) continue;
      packets = *p;
      bytes = *b;
      line.remove_prefix(close + 1U);
    }

    const auto tokens = tokenize(line);
    if (tokens.size() < 2U || tokens[0] != "-A") continue;  // :CHAIN, COMMIT, -P

    IptablesCounterRule rule;
    rule.table = table;
    rule.chain = tokens[1];
    rule.packets = packets;
    rule.bytes = bytes;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
      const auto& token = tokens[i];
      if (token == "--comment" && i + 1U < tokens.size()) {
        rule.comment = tokens[++i];
      } else if ((token == "-j" || token == "-g") && i + 1U < tokens.size()) {
        rule.target = tokens[++i];
      } else if ((token == "--set-xmark" || token == "--set-mark") &&
                 i + 1U < tokens.size()) {
        rule.mark_value = parse_mark_value(tokens[++i]);
      }
    }
    rules.push_back(std::move(rule));
  }
  return rules;
}

FirewallCounterIndex build_firewall_counter_index(const FirewallPlan& plan,
                                                  const OutboundMarkMap& marks) {
  FirewallCounterIndex index;
  for (const auto& [tag, mark] : marks) {
    if (mark != 0) index.mark_outbound.emplace(mark, tag);
  }
  for (const auto& rule : plan.rules) {
    const auto* balance = std::get_if<BalanceAction>(&rule.action);
    if (balance == nullptr) continue;
    const auto outbound = index.mark_outbound.find(balance->fallback_mark);
    if (outbound == index.mark_outbound.end()) continue;
    try {
      index.balance_comment_outbound[rule.key.comment()] = outbound->second;
    } catch (const std::exception&) {
      // A key without a representable comment has no rule to read.
    }
  }
  return index;
}

void aggregate_firewall_counters(const std::vector<IptablesCounterRule>& rules,
                                 const std::string& family,
                                 const FirewallCounterIndex& index,
                                 FirewallCounters& into) {
  for (const auto& rule : rules) {
    if (rule.comment.empty()) continue;  // foreign rule
    if (starts_with(rule.comment, kSkipMarkedComment) &&
        rule.target == "ACCEPT") {
      into.skip_marked_packets[family] += rule.packets;
      continue;
    }
    // Candidate rules are the cascade's MARK rules; the CONNMARK save and the
    // RETURN carry the same comment but are not classifications.
    if (rule.target != "MARK" || !rule.mark_value.has_value()) continue;
    const auto outbound = index.balance_comment_outbound.find(rule.comment);
    if (outbound == index.balance_comment_outbound.end()) continue;
    const auto candidate = index.mark_outbound.find(*rule.mark_value);
    const std::string candidate_name = candidate == index.mark_outbound.end()
                                           ? mark_label(*rule.mark_value)
                                           : candidate->second;
    auto& list = into.balance_classifications;
    auto existing = std::find_if(
        list.begin(), list.end(), [&](const FirewallCounters::Classification& c) {
          return c.outbound == outbound->second && c.candidate == candidate_name &&
                 c.family == family;
        });
    if (existing == list.end()) {
      list.push_back({outbound->second, candidate_name, family, rule.packets});
    } else {
      existing->connections += rule.packets;
    }
  }
}

FirewallCounters collect_iptables_counters(const CommandRunner& runner,
                                           const FirewallCounterIndex& index,
                                           bool ipv6_enabled,
                                           RawPreroutingMode raw_prerouting) {
  FirewallCounters counters;
  const auto read_family = [&](bool ipv6) {
    const char* command = ipv6 ? "ip6tables-save" : "iptables-save";
    const std::string family = ipv6 ? "ipv6" : "ipv4";
    std::vector<IptablesCounterRule> rules;
    std::vector<const char*> tables{"mangle"};
    if (raw_prerouting.uses(ipv6)) tables.push_back("raw");
    for (const char* table : tables) {
      const auto result = runner({command, "-c", "-t", table});
      if (result.exit_code != 0 || result.truncated) return;
      auto parsed = parse_iptables_save_counters(result.stdout_output);
      rules.insert(rules.end(), std::make_move_iterator(parsed.begin()),
                   std::make_move_iterator(parsed.end()));
    }
    aggregate_firewall_counters(rules, family, index, counters);
  };
  read_family(false);
  if (ipv6_enabled) read_family(true);
  return counters;
}

FirewallCounters FirewallCounterCache::get(
    const void* generation, const std::function<FirewallCounters()>& read) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = Clock::now();
  if (valid_ && generation_ == generation && now - read_at_ < lifetime_) {
    return counters_;
  }
  counters_ = read();
  generation_ = generation;
  read_at_ = now;
  valid_ = true;
  return counters_;
}

} // namespace keen_pbr3
