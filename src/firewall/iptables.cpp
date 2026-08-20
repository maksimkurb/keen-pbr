#include "iptables.hpp"
#include "../log/logger.hpp"
#include "../util/format_compat.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/safe_exec.hpp"
#include "ipset_restore_pipe.hpp"
#include "port_spec_util.hpp"
#include <rapidxml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>

namespace keen_pbr3 {

namespace {

bool is_ipv6_addr(const std::string &addr) {
  return addr.find(':') != std::string::npos;
}

std::vector<std::string>
filter_addrs_by_family(const std::vector<std::string> &addrs, bool ipv6) {
  std::vector<std::string> filtered;
  for (const auto &addr : addrs) {
    if (is_ipv6_addr(addr) == ipv6) {
      filtered.push_back(addr);
    }
  }
  return filtered;
}

std::vector<L4Proto> expand_l4_protos(L4Proto proto) {
  if (proto == L4Proto::TcpUdp) {
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  return {proto};
}

std::vector<L4Proto>
expand_l4_protos_for_iptables(const FirewallRuleCriteria &criteria) {
  if (criteria.proto == L4Proto::Any &&
      (!criteria.src_port.empty() || !criteria.dst_port.empty())) {
    // iptables requires an explicit L4 protocol whenever port matchers are
    // used.
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  return expand_l4_protos(criteria.proto);
}

} // namespace

IptablesFirewall::IptablesFirewall(bool use_raw_prerouting)
    : use_raw_prerouting_(use_raw_prerouting) {
  if (use_raw_prerouting_) {
    validate_raw_prerouting_capability();
  }
}

const char *
IptablesFirewall::generation_chain(FirewallSetGeneration generation) {
  return generation == FirewallSetGeneration::A ? "KeenPbrTable_A"
                                                : "KeenPbrTable_B";
}

const char *
IptablesFirewall::output_generation_chain(FirewallSetGeneration generation) {
  return generation == FirewallSetGeneration::A ? "KeenPbrOutput_A"
                                                : "KeenPbrOutput_B";
}

const char *IptablesFirewall::prerouting_table_name(bool ipv6) const {
  return use_raw_prerouting_ && !ipv6 ? "raw" : "mangle";
}

const char *
IptablesFirewall::prerouting_dispatcher_chain_name(bool ipv6) const {
  return use_raw_prerouting_ && !ipv6 ? RAW_CHAIN_NAME : CHAIN_NAME;
}

const char *
IptablesFirewall::prerouting_generation_chain(FirewallSetGeneration generation,
                                              bool ipv6) const {
  return use_raw_prerouting_ && !ipv6
             ? (generation == FirewallSetGeneration::A ? "KeenPbrRaw_A"
                                                       : "KeenPbrRaw_B")
             : generation_chain(generation);
}

void IptablesFirewall::validate_raw_prerouting_capability() const {
  std::ifstream tables("/proc/net/ip_tables_names");
  std::string table;
  bool raw_present = false;
  while (std::getline(tables, table)) {
    if (table == "raw") {
      raw_present = true;
      break;
    }
  }
  struct utsname uts{};
  const std::string module =
      uname(&uts) == 0
          ? std::string("/lib/modules/") + uts.release + "/iptable_raw.ko"
          : "/lib/modules/$(uname -r)/iptable_raw.ko";
  const int probe =
      safe_exec({"iptables", "-t", "raw", "-S"}, /*suppress_output=*/true);
  if (!raw_present || probe != 0) {
    throw FirewallError(
        "--use-raw-prerouting requested, but raw table is unavailable "
        "(expected module " +
        module +
        ", iptables -t raw -S failed); "
        "no fallback to mangle PREROUTING was performed");
  }
}

void IptablesFirewall::prepare_apply(FirewallApplyMode mode) {
  pending_sets_.clear();
  pending_elements_.clear();
  pending_rules_.clear();
  prepared_mode_ = mode;

  if (mode == FirewallApplyMode::RulesOnly) {
    // RulesOnly preparation is deliberately inspection-only. In particular,
    // do not repair an interrupted OUTPUT publication until apply() has
    // preflighted every reused set successfully.
    const auto v4 = inspect_generation(false);
    target_v4_generation_ =
        generation_plan_for_states(v4.primary, v4.secondary).target;
    if (v4.primary == LiveGenerationState::A) {
      target_static_v4_generation_ = FirewallSetGeneration::A;
    } else if (v4.primary == LiveGenerationState::B) {
      target_static_v4_generation_ = FirewallSetGeneration::B;
    } else {
      throw FirewallRulesOnlyError(
          "cannot reuse static IPv4 ipsets: live PREROUTING generation is "
          "missing or invalid");
    }

    if (!ipv6_enabled() || !ipv6_backend_available()) {
      target_v6_generation_ = FirewallSetGeneration::A;
      target_static_v6_generation_ = FirewallSetGeneration::A;
    } else {
      const auto v6 = inspect_generation(true);
      target_v6_generation_ =
          generation_plan_for_states(v6.primary, v6.secondary).target;
      if (v6.primary == LiveGenerationState::A) {
        target_static_v6_generation_ = FirewallSetGeneration::A;
      } else if (v6.primary == LiveGenerationState::B) {
        target_static_v6_generation_ = FirewallSetGeneration::B;
      } else {
        throw FirewallRulesOnlyError(
            "cannot reuse static IPv6 ipsets: live PREROUTING generation is "
            "missing or invalid");
      }
    }
  } else {
    target_v4_generation_ =
        mode == FirewallApplyMode::Destructive
            ? FirewallSetGeneration::A
            : select_target_generation(false);
    target_v6_generation_ =
        mode == FirewallApplyMode::Destructive
            ? FirewallSetGeneration::A
            : (!ipv6_enabled() || !ipv6_backend_available()
                   ? FirewallSetGeneration::A
                   : select_target_generation(true));
    target_static_v4_generation_ = target_v4_generation_;
    target_static_v6_generation_ = target_v6_generation_;
  }
  static_generations_prepared_ = true;
  apply_prepared_ = true;
}

std::string IptablesFirewall::static_set_name(const std::string &list_name,
                                              int family) const {
  const FirewallSetGeneration generation =
      !static_generations_prepared_
          ? (family == AF_INET6 ? target_v6_generation_ : target_v4_generation_)
          : (family == AF_INET6 ? target_static_v6_generation_
                                : target_static_v4_generation_);
  const char slot = generation == FirewallSetGeneration::A ? 's' : 'S';
  return keen_pbr3::format("kpbr{}{}_{}", family == AF_INET6 ? 6 : 4, slot,
                           list_name);
}

void IptablesFirewall::create_ipset(const std::string &set_name, int family,
                                    uint32_t timeout) {
  PendingSet ps;
  ps.name = set_name;
  ps.family_str = (family == AF_INET6) ? "inet6" : "inet";
  ps.timeout = timeout;
  const auto existing = std::find_if(pending_sets_.begin(), pending_sets_.end(),
                                     [&set_name](const PendingSet &pending) {
                                       return pending.name == set_name;
                                     });
  if (existing == pending_sets_.end()) {
    pending_sets_.push_back(std::move(ps));
  } else if (existing->family_str != ps.family_str ||
             existing->timeout != ps.timeout) {
    throw FirewallError("conflicting ipset declaration for " + set_name);
  }
  created_sets_[set_name] = family;
}

void IptablesFirewall::append_rules_for_family(
    bool ipv6, PendingRule::Action action, uint32_t fwmark,
    const FirewallRuleCriteria &criteria) {
  const std::vector<std::string> any_addr{""};
  const auto filtered_src_addrs =
      criteria.src_addr.empty()
          ? any_addr
          : filter_addrs_by_family(criteria.src_addr, ipv6);
  const auto filtered_dst_addrs =
      criteria.dst_addr.empty()
          ? any_addr
          : filter_addrs_by_family(criteria.dst_addr, ipv6);
  if ((!criteria.src_addr.empty() && filtered_src_addrs.empty()) ||
      (!criteria.dst_addr.empty() && filtered_dst_addrs.empty())) {
    return;
  }

  for (const auto proto : expand_l4_protos_for_iptables(criteria)) {
    const std::vector<std::string> &src_addrs = filtered_src_addrs;
    const std::vector<std::string> &dst_addrs = filtered_dst_addrs;
    for (const auto &src : src_addrs) {
      for (const auto &dst : dst_addrs) {
        PendingRule pr;
        pr.ipv6 = ipv6;
        pr.action = action;
        pr.fwmark = fwmark;
        pr.fwmark_mask = fwmark_mask();
        pr.criteria = criteria;
        pr.criteria.proto = proto;
        pr.criteria.src_addr = src.empty() ? std::vector<std::string>{}
                                           : std::vector<std::string>{src};
        pr.criteria.dst_addr = dst.empty() ? std::vector<std::string>{}
                                           : std::vector<std::string>{dst};
        pending_rules_.push_back(std::move(pr));
      }
    }
  }
}

void IptablesFirewall::create_mark_rule(uint32_t fwmark,
                                        const FirewallRuleCriteria &criteria) {
  if (criteria.dst_set_name.has_value()) {
    auto it = created_sets_.find(*criteria.dst_set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    append_rules_for_family(ipv6, PendingRule::Mark, fwmark, criteria);
    return;
  }
  append_rules_for_family(false, PendingRule::Mark, fwmark, criteria);
  append_rules_for_family(true, PendingRule::Mark, fwmark, criteria);
}

void IptablesFirewall::create_drop_rule(const FirewallRuleCriteria &criteria) {
  if (criteria.dst_set_name.has_value()) {
    auto it = created_sets_.find(*criteria.dst_set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    append_rules_for_family(ipv6, PendingRule::Drop, 0, criteria);
    return;
  }
  append_rules_for_family(false, PendingRule::Drop, 0, criteria);
  append_rules_for_family(true, PendingRule::Drop, 0, criteria);
}

void IptablesFirewall::create_pass_rule(const FirewallRuleCriteria &criteria) {
  if (criteria.dst_set_name.has_value()) {
    auto it = created_sets_.find(*criteria.dst_set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    append_rules_for_family(ipv6, PendingRule::Pass, 0, criteria);
    return;
  }
  append_rules_for_family(false, PendingRule::Pass, 0, criteria);
  append_rules_for_family(true, PendingRule::Pass, 0, criteria);
}

std::unique_ptr<ListEntryVisitor>
IptablesFirewall::create_batch_loader(const std::string &set_name) {
  if (prepared_mode_ == FirewallApplyMode::RulesOnly) {
    throw FirewallRulesOnlyError(
        "RulesOnly cannot stream or modify set " + set_name);
  }
  auto &buf = pending_elements_[set_name];
  return std::make_unique<IpsetRestoreVisitor>(buf, set_name);
}

static void pipe_to_cmd(const std::vector<std::string> &args,
                        const std::string &input) {
  Logger::instance().verbose("{} script:\n{}", args[0], input);
  int status = safe_exec_pipe_stdin(args, input);
  if (status != 0) {
    throw FirewallError(
        keen_pbr3::format("{} exited with status {}", args[0], status));
  }
}

std::string IptablesFirewall::build_ipset_create_line(const PendingSet &ps) {
  if (ps.timeout > 0) {
    return keen_pbr3::format("create {} hash:net family {} timeout {} -exist\n",
                             ps.name, ps.family_str, ps.timeout);
  } else {
    return keen_pbr3::format("create {} hash:net family {} -exist\n", ps.name,
                             ps.family_str);
  }
}

bool IptablesFirewall::is_dynamic_set_name(const std::string &set_name) {
  return set_name.rfind("kpbr4d_", 0) == 0 || set_name.rfind("kpbr6d_", 0) == 0;
}

bool IptablesFirewall::dynamic_set_schema_compatible(
    const std::string &xml, const PendingSet &expected) {
  if (xml.empty() || xml.find('\0') != std::string::npos) {
    return false;
  }

  std::vector<char> buffer(xml.begin(), xml.end());
  buffer.push_back('\0');
  rapidxml::xml_document<> document;
  try {
    document.parse<rapidxml::parse_trim_whitespace |
                   rapidxml::parse_validate_closing_tags>(buffer.data());
  } catch (const rapidxml::parse_error &) {
    return false;
  }

  auto *root = document.first_node("ipsets");
  if (root == nullptr || document.first_node() != root ||
      root->next_sibling() != nullptr) {
    return false;
  }
  auto *set = root->first_node("ipset");
  if (set == nullptr || set->next_sibling("ipset") != nullptr) {
    return false;
  }
  for (auto *child = root->first_node(); child != nullptr;
       child = child->next_sibling()) {
    if (child->type() == rapidxml::node_element && child != set) {
      return false;
    }
  }
  auto *name = set->first_attribute("name");
  auto *type = set->first_node("type");
  auto *header = set->first_node("header");
  if (name == nullptr || type == nullptr || header == nullptr ||
      set->last_attribute("name") != name || set->last_node("type") != type ||
      set->last_node("header") != header) {
    return false;
  }
  auto *family = header->first_node("family");
  auto *timeout_node = header->first_node("timeout");
  if (family == nullptr || header->last_node("family") != family ||
      (timeout_node != nullptr &&
       header->last_node("timeout") != timeout_node)) {
    return false;
  }

  const std::string_view live_name(name->value(), name->value_size());
  const std::string_view live_type(type->value(), type->value_size());
  const std::string_view live_family(family->value(), family->value_size());
  uint32_t live_timeout = 0;
  if (timeout_node != nullptr) {
    const char *begin = timeout_node->value();
    const char *end = begin + timeout_node->value_size();
    const auto parsed = std::from_chars(begin, end, live_timeout);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return false;
    }
  }
  return live_name == expected.name && live_type == "hash:net" &&
         live_family == expected.family_str &&
         live_timeout == expected.timeout;
}

void IptablesFirewall::preflight_dynamic_set_schemas(
    bool effective_ipv6) const {
  const bool has_dynamic = std::any_of(
      pending_sets_.begin(), pending_sets_.end(), [&](const PendingSet &set) {
        return is_dynamic_set_name(set.name) &&
               (set.family_str != "inet6" || effective_ipv6);
      });
  if (!has_dynamic) {
    return;
  }

  const auto names =
      safe_exec_capture({"ipset", "list", "-n"}, /*suppress_stderr=*/true);
  if (names.exit_code != 0 || names.truncated || names.timed_out) {
    throw FirewallError("failed to inspect dynamic ipset schemas");
  }
  std::set<std::string> live_names;
  std::istringstream name_lines(names.stdout_output);
  std::string live_name;
  while (std::getline(name_lines, live_name)) {
    if (!live_name.empty()) {
      live_names.insert(live_name);
    }
  }
  for (const auto &set : pending_sets_) {
    if (!is_dynamic_set_name(set.name) ||
        (set.family_str == "inet6" && !effective_ipv6)) {
      continue;
    }
    if (live_names.find(set.name) == live_names.end()) {
      continue;
    }
    const auto schema = safe_exec_capture(
        {"ipset", "list", "-t", set.name, "-o", "xml"},
        /*suppress_stderr=*/true);
    if (schema.exit_code != 0 || schema.truncated || schema.timed_out) {
      throw FirewallError("failed to inspect dynamic ipset schema for " +
                          set.name);
    }
    if (!dynamic_set_schema_compatible(schema.stdout_output, set)) {
      throw FirewallError("incompatible existing dynamic ipset schema for " +
                          set.name);
    }
  }
}

void IptablesFirewall::preflight_reused_set_schemas(
    bool effective_ipv6) const {
  const auto names =
      safe_exec_capture({"ipset", "list", "-n"}, /*suppress_stderr=*/true);
  if (names.exit_code != 0 || names.truncated || names.timed_out) {
    throw FirewallRulesOnlyError(
        "failed to inspect required reused ipset names");
  }

  std::set<std::string> live_names;
  std::istringstream name_lines(names.stdout_output);
  std::string live_name;
  while (std::getline(name_lines, live_name)) {
    if (!live_name.empty()) {
      live_names.insert(live_name);
    }
  }

  for (const auto &rule : pending_rules_) {
    if (!rule.criteria.dst_set_name.has_value()) {
      continue;
    }
    const auto expected = std::find_if(
        pending_sets_.begin(), pending_sets_.end(), [&](const PendingSet &set) {
          return set.name == *rule.criteria.dst_set_name &&
                 ((rule.ipv6 && set.family_str == "inet6") ||
                  (!rule.ipv6 && set.family_str == "inet"));
        });
    if (expected == pending_sets_.end()) {
      throw FirewallRulesOnlyError(
          "required reused ipset " + *rule.criteria.dst_set_name +
          " has no compatible declaration for the packet family");
    }
  }

  for (const auto &set : pending_sets_) {
    if (set.family_str == "inet6" && !effective_ipv6) {
      continue;
    }
    if (live_names.find(set.name) == live_names.end()) {
      throw FirewallRulesOnlyError(
          "required reused ipset " + set.name + " is missing");
    }
    const auto schema = safe_exec_capture(
        {"ipset", "list", "-t", set.name, "-o", "xml"},
        /*suppress_stderr=*/true);
    if (schema.exit_code != 0 || schema.truncated || schema.timed_out) {
      throw FirewallRulesOnlyError(
          "failed to inspect required reused ipset schema for " + set.name);
    }
    if (!dynamic_set_schema_compatible(schema.stdout_output, set)) {
      throw FirewallRulesOnlyError(
          "required reused ipset " + set.name +
          " has incompatible family, type, or timeout schema");
    }
  }
}

bool IptablesFirewall::ipv6_backend_available() const {
  return iptables_ipv6_supported();
}

IptablesFirewall::LiveGenerationState
IptablesFirewall::inspect_live_generation(bool ipv6) const {
  const char *command = ipv6 ? "ip6tables" : "iptables";
  const std::string dispatcher = prerouting_dispatcher_chain_name(ipv6);
  return inspect_dispatcher(
      command, prerouting_table_name(ipv6), dispatcher,
      prerouting_generation_chain(FirewallSetGeneration::A, ipv6),
      prerouting_generation_chain(FirewallSetGeneration::B, ipv6));
}

IptablesFirewall::GenerationInspection
IptablesFirewall::inspect_generation(bool ipv6) const {
  const auto primary = inspect_live_generation(ipv6);
  if (primary == LiveGenerationState::Invalid) {
    throw FirewallError("damaged iptables PREROUTING dispatcher");
  }

  const char *command = ipv6 ? "ip6tables" : "iptables";
  const std::string output_dispatcher =
      use_raw_prerouting_ && !ipv6 ? OUTPUT_CHAIN_NAME
                                  : std::string(CHAIN_NAME) + "_OUTPUT";
  const auto secondary = inspect_dispatcher(
      command, "mangle", output_dispatcher,
      use_raw_prerouting_ && !ipv6
          ? output_generation_chain(FirewallSetGeneration::A)
          : generation_chain(FirewallSetGeneration::A),
      use_raw_prerouting_ && !ipv6
          ? output_generation_chain(FirewallSetGeneration::B)
          : generation_chain(FirewallSetGeneration::B));
  if (secondary == LiveGenerationState::Invalid) {
    throw FirewallError("damaged iptables OUTPUT dispatcher");
  }
  return {primary, secondary};
}

IptablesFirewall::GenerationPlan IptablesFirewall::generation_plan_for_states(
    LiveGenerationState primary, LiveGenerationState secondary) {
  if (primary == LiveGenerationState::Invalid ||
      secondary == LiveGenerationState::Invalid) {
    throw FirewallError("cannot select a target from a damaged dispatcher");
  }
  return {target_generation_for_states(primary, secondary),
          (primary == LiveGenerationState::A &&
           secondary == LiveGenerationState::B) ||
              (primary == LiveGenerationState::B &&
               secondary == LiveGenerationState::A)};
}

IptablesFirewall::LiveGenerationState IptablesFirewall::inspect_dispatcher(
    const char *command, const char *table, const std::string &dispatcher,
    const std::string &generation_a, const std::string &generation_b) const {
  const auto result = safe_exec_capture(
      {command, "-t", table, "-S"},
      /*suppress_stderr=*/true);
  if (result.exit_code != 0 || result.truncated || result.timed_out) {
    throw FirewallError("failed to inspect live iptables dispatcher " +
                        dispatcher);
  }
  return parse_live_generation(result.stdout_output, dispatcher, generation_a,
                               generation_b);
}

FirewallSetGeneration
IptablesFirewall::select_target_generation(bool ipv6) const {
  const auto inspection = inspect_generation(ipv6);
  const auto primary = inspection.primary;
  const auto secondary = inspection.secondary;
  const auto plan = generation_plan_for_states(primary, secondary);

  if (plan.repair_output) {
    // OUTPUT is published before PREROUTING. A failed apply can therefore
    // leave OUTPUT one generation ahead. Restore it to the authoritative
    // PREROUTING generation before selecting the now-inactive slot.
    publish_dispatcher(ipv6, /*output=*/true,
                       primary == LiveGenerationState::A
                           ? FirewallSetGeneration::A
                           : FirewallSetGeneration::B);
  }

  return plan.target;
}

void IptablesFirewall::ensure_target_generation_inactive(
    bool ipv6, FirewallSetGeneration target) const {
  const auto selected = select_target_generation(ipv6);
  if (selected != target) {
    throw FirewallError(
        "live iptables generation changed while preparing apply");
  }
}

void IptablesFirewall::publish_dispatcher(
    bool ipv6, bool output, FirewallSetGeneration generation) const {
  const char *command = ipv6 ? "ip6tables-restore" : "iptables-restore";
  const bool raw_output = output && use_raw_prerouting_ && !ipv6;
  const char *table = output ? "mangle" : prerouting_table_name(ipv6);
  const std::string dispatcher =
      output ? (raw_output ? OUTPUT_CHAIN_NAME
                           : std::string(CHAIN_NAME) + "_OUTPUT")
             : prerouting_dispatcher_chain_name(ipv6);
  const std::string target =
      raw_output ? output_generation_chain(generation)
                 : prerouting_generation_chain(generation, ipv6);
  const std::string script =
      keen_pbr3::format("*{}\n:{} - [0:0]\n-A {} -j {}\nCOMMIT\n", table,
                        dispatcher, dispatcher, target);
  pipe_to_cmd({command, "--noflush", "--counters"}, script);
}

IptablesFirewall::LiveGenerationState IptablesFirewall::parse_live_generation(
    const std::string &rules, const std::string &dispatcher,
    const std::string &generation_a, const std::string &generation_b) {
  const std::string jump_a = "-A " + dispatcher + " -j " + generation_a;
  const std::string jump_b = "-A " + dispatcher + " -j " + generation_b;
  size_t a_count = 0;
  size_t b_count = 0;
  size_t other_rule_count = 0;
  std::istringstream input(rules);
  std::string line;
  while (std::getline(input, line)) {
    if (line == jump_a) {
      ++a_count;
    } else if (line == jump_b) {
      ++b_count;
    } else if (line.rfind("-A " + dispatcher + " ", 0) == 0) {
      ++other_rule_count;
    }
  }
  if (a_count == 1 && b_count == 0 && other_rule_count == 0) {
    return LiveGenerationState::A;
  }
  if (b_count == 1 && a_count == 0 && other_rule_count == 0) {
    return LiveGenerationState::B;
  }
  if (a_count == 0 && b_count == 0 && other_rule_count == 0) {
    return LiveGenerationState::Missing;
  }
  return LiveGenerationState::Invalid;
}

FirewallSetGeneration IptablesFirewall::target_generation_for_states(
    LiveGenerationState primary, LiveGenerationState secondary) {
  if (primary == LiveGenerationState::Invalid ||
      secondary == LiveGenerationState::Invalid) {
    throw FirewallError("cannot select a target from a damaged dispatcher");
  }
  if (primary == LiveGenerationState::A) {
    return FirewallSetGeneration::B;
  }
  if (primary == LiveGenerationState::B) {
    return FirewallSetGeneration::A;
  }
  if (secondary == LiveGenerationState::A)
    return FirewallSetGeneration::B;
  if (secondary == LiveGenerationState::B)
    return FirewallSetGeneration::A;
  return FirewallSetGeneration::A;
}

size_t IptablesFirewall::count_exact_jump(const std::string &rules,
                                          const std::string &source_chain,
                                          const std::string &target_chain) {
  const std::string expected = "-A " + source_chain + " -j " + target_chain;
  size_t count = 0;
  std::istringstream input(rules);
  std::string line;
  while (std::getline(input, line)) {
    if (line == expected) {
      ++count;
    }
  }
  return count;
}

void IptablesFirewall::reconcile_hook(const char *command, const char *table,
                                      const char *builtin_chain,
                                      const char *target_chain) {
  const auto result =
      safe_exec_capture({command, "-t", table, "-S", builtin_chain},
                        /*suppress_stderr=*/true);
  if (result.exit_code != 0 || result.truncated || result.timed_out) {
    throw FirewallError(keen_pbr3::format("failed to inspect {} {}/{} hook",
                                          command, table, builtin_chain));
  }
  size_t count =
      count_exact_jump(result.stdout_output, builtin_chain, target_chain);
  if (count == 0) {
    if (safe_exec(
            {command, "-t", table, "-A", builtin_chain, "-j", target_chain},
            /*suppress_output=*/true) != 0) {
      throw FirewallError(keen_pbr3::format("failed to add {} {}/{} hook",
                                            command, table, builtin_chain));
    }
    return;
  }
  while (count > 1) {
    if (safe_exec(
            {command, "-t", table, "-D", builtin_chain, "-j", target_chain},
            /*suppress_output=*/true) != 0) {
      throw FirewallError(
          keen_pbr3::format("failed to remove duplicate {} {}/{} hook", command,
                            table, builtin_chain));
    }
    --count;
  }
}

void IptablesFirewall::remove_all_hooks(const char *command, const char *table,
                                        const char *builtin_chain,
                                        const char *target_chain) {
  const auto result =
      safe_exec_capture({command, "-t", table, "-S", builtin_chain},
                        /*suppress_stderr=*/true);
  if (result.exit_code != 0 || result.truncated || result.timed_out) {
    return;
  }
  const size_t observed =
      count_exact_jump(result.stdout_output, builtin_chain, target_chain);
  for (size_t i = 0; i < observed; ++i) {
    if (safe_exec(
            {command, "-t", table, "-D", builtin_chain, "-j", target_chain},
            /*suppress_output=*/true) != 0) {
      break;
    }
  }
}

void IptablesFirewall::reconcile_hooks(bool ipv6) const {
  const char *command = ipv6 ? "ip6tables" : "iptables";
  reconcile_hook(command, prerouting_table_name(ipv6), "PREROUTING",
                 prerouting_dispatcher_chain_name(ipv6));
  const char *output_dispatcher =
      use_raw_prerouting_ && !ipv6 ? OUTPUT_CHAIN_NAME : "KeenPbrTable_OUTPUT";
  reconcile_hook(command, "mangle", "OUTPUT", output_dispatcher);
}

void IptablesFirewall::verify_applied_generation(
    bool ipv6, FirewallSetGeneration target) const {
  const char *command = ipv6 ? "ip6tables" : "iptables";
  const std::string prerouting_dispatcher =
      prerouting_dispatcher_chain_name(ipv6);
  const auto prerouting = safe_exec_capture(
      {command, "-t", prerouting_table_name(ipv6), "-S", prerouting_dispatcher},
      /*suppress_stderr=*/true);
  const auto expected_state = target == FirewallSetGeneration::A
                                  ? LiveGenerationState::A
                                  : LiveGenerationState::B;
  if (prerouting.exit_code != 0 || prerouting.truncated ||
      prerouting.timed_out ||
      parse_live_generation(
          prerouting.stdout_output, prerouting_dispatcher,
          prerouting_generation_chain(FirewallSetGeneration::A, ipv6),
          prerouting_generation_chain(FirewallSetGeneration::B, ipv6)) !=
          expected_state) {
    throw FirewallError("iptables PREROUTING dispatcher verification failed");
  }

  const std::string output_dispatcher =
      use_raw_prerouting_ && !ipv6 ? OUTPUT_CHAIN_NAME
                                   : std::string(CHAIN_NAME) + "_OUTPUT";
  const std::string output_a =
      use_raw_prerouting_ && !ipv6
          ? output_generation_chain(FirewallSetGeneration::A)
          : generation_chain(FirewallSetGeneration::A);
  const std::string output_b =
      use_raw_prerouting_ && !ipv6
          ? output_generation_chain(FirewallSetGeneration::B)
          : generation_chain(FirewallSetGeneration::B);
  const auto output =
      safe_exec_capture({command, "-t", "mangle", "-S", output_dispatcher},
                        /*suppress_stderr=*/true);
  if (output.exit_code != 0 || output.truncated || output.timed_out ||
      parse_live_generation(output.stdout_output, output_dispatcher, output_a,
                            output_b) != expected_state) {
    throw FirewallError("iptables OUTPUT dispatcher verification failed");
  }

  const auto prerouting_hook = safe_exec_capture(
      {command, "-t", prerouting_table_name(ipv6), "-S", "PREROUTING"},
      /*suppress_stderr=*/true);
  const auto output_hook =
      safe_exec_capture({command, "-t", "mangle", "-S", "OUTPUT"},
                        /*suppress_stderr=*/true);
  if (prerouting_hook.exit_code != 0 || output_hook.exit_code != 0 ||
      count_exact_jump(prerouting_hook.stdout_output, "PREROUTING",
                       prerouting_dispatcher) != 1 ||
      count_exact_jump(output_hook.stdout_output, "OUTPUT",
                       output_dispatcher) != 1) {
    throw FirewallError("iptables builtin hook verification failed");
  }
}

std::vector<std::string> IptablesFirewall::build_proto_port_fragments(
    L4Proto proto, const PortSpec &src_port, const PortSpec &dst_port,
    bool negate_src_port, bool negate_dst_port) {
  if (proto == L4Proto::Any && src_port.empty() && dst_port.empty()) {
    return {""};
  }

  const std::string proto_frag =
      proto == L4Proto::Any ? "" : " -p " + std::string(l4_proto_name(proto));

  auto chunks = [](const PortSpec &spec) {
    std::vector<PortSpec> result;
    PortSpec chunk;
    size_t slots = 0;
    for (const auto &range : spec.ranges) {
      const size_t range_slots = range.from == range.to ? 1 : 2;
      if (!chunk.ranges.empty() && slots + range_slots > 15) {
        result.push_back(std::move(chunk));
        chunk = PortSpec{};
        slots = 0;
      }
      chunk.ranges.push_back(range);
      slots += range_slots;
    }
    if (!chunk.ranges.empty()) {
      result.push_back(std::move(chunk));
    }
    return result;
  };

  auto side_fragments = [&](const PortSpec &spec, bool source, bool negated) {
    if (spec.empty()) {
      return std::vector<std::string>{""};
    }
    const bool list = classify_port_spec(spec) == PortSpecKind::List;
    const std::string singular = source ? " --sport " : " --dport ";
    const std::string plural = source ? " --sports " : " --dports ";
    if (!list) {
      return std::vector<std::string>{
          std::string(negated ? " !" : "") + singular +
          spec.to_iptables_string()};
    }

    const auto list_chunks = chunks(spec);
    if (negated) {
      std::string fragment;
      for (const auto &chunk : list_chunks) {
        fragment +=
            " -m multiport !" + plural + chunk.to_iptables_string();
      }
      return std::vector<std::string>{std::move(fragment)};
    }

    std::vector<std::string> result;
    result.reserve(list_chunks.size());
    for (const auto &chunk : list_chunks) {
      result.push_back(" -m multiport" + plural +
                       chunk.to_iptables_string());
    }
    return result;
  };

  const auto src_fragments =
      side_fragments(src_port, /*source=*/true, negate_src_port);
  const auto dst_fragments =
      side_fragments(dst_port, /*source=*/false, negate_dst_port);
  std::vector<std::string> fragments;
  fragments.reserve(src_fragments.size() * dst_fragments.size());
  for (const auto &src : src_fragments) {
    for (const auto &dst : dst_fragments) {
      fragments.push_back(proto_frag + src + dst);
    }
  }
  return fragments;
}

std::string IptablesFirewall::build_prefilter_lines(
    const FirewallGlobalPrefilter &prefilter, const std::string &chain,
    bool allow_conntrack) {
  std::string lines;
  // raw PREROUTING runs before conntrack.  Do not rely on CONNMARK, ctstate,
  // or ctdir there: every forwarded packet is classified directly instead.
  if (allow_conntrack && prefilter.restore_conntrack_mark &&
      prefilter.conntrack_mark_mask != 0) {
    const std::string mask =
        keen_pbr3::format("{:#x}", prefilter.conntrack_mark_mask);
    lines += keen_pbr3::format(
        "-A {} -m conntrack --ctdir ORIGINAL -m connmark ! --mark 0/{} "
        "-j CONNMARK --restore-mark --mask {}\n"
        "-A {} -m conntrack --ctdir ORIGINAL -m mark ! --mark 0/{} -j RETURN\n",
        chain, mask, mask, chain, mask);
  }
  if (allow_conntrack && prefilter.skip_established_or_dnat) {
    lines += keen_pbr3::format("-A {} -m conntrack --ctstate DNAT -j RETURN\n",
                               chain);
  }

  if (prefilter.skip_marked_packets) {
    lines += keen_pbr3::format(
        "-A {} -m mark ! --mark 0x0/0xffffffff -j ACCEPT\n", chain);
  }

  if (prefilter.has_inbound_interfaces() &&
      prefilter.inbound_interfaces.has_value() &&
      prefilter.inbound_interfaces->size() == 1) {
    lines += keen_pbr3::format("-A {} ! -i {} -j RETURN\n", chain,
                               prefilter.inbound_interfaces->front());
  }

  return lines;
}

std::vector<std::string> IptablesFirewall::build_rule_lines(
    const PendingRule &pr, const FirewallGlobalPrefilter &prefilter,
    const std::string &chain, bool allow_conntrack) {
  // iptables cannot express a multi-value negated -i guard in one rule, so
  // multi-interface allowlists are expanded into one positive -i match per
  // rule.
  std::vector<std::string> iface_frags;
  if (prefilter.has_inbound_interfaces() &&
      prefilter.inbound_interfaces.has_value() &&
      prefilter.inbound_interfaces->size() > 1) {
    iface_frags.reserve(prefilter.inbound_interfaces->size());
    for (const auto &iface : *prefilter.inbound_interfaces) {
      iface_frags.push_back(" -i " + iface);
    }
  } else {
    iface_frags.push_back("");
  }

  std::string addr_frag;
  if (!pr.criteria.src_addr.empty())
    addr_frag += std::string(pr.criteria.negate_src_addr ? " !" : "") + " -s " +
                 pr.criteria.src_addr[0];
  if (!pr.criteria.dst_addr.empty())
    addr_frag += std::string(pr.criteria.negate_dst_addr ? " !" : "") + " -d " +
                 pr.criteria.dst_addr[0];
  std::string dscp_frag;
  if (pr.criteria.dscp.has_value()) {
    dscp_frag = keen_pbr3::format(" -m dscp --dscp {}",
                                  static_cast<int>(*pr.criteria.dscp));
  }
  std::vector<std::string> lines;
  lines.reserve(iface_frags.size() * 2);
  auto append_mark_and_save = [&](std::string mark_line) {
    lines.push_back(mark_line);
    if (!allow_conntrack || !prefilter.restore_conntrack_mark ||
        prefilter.conntrack_mark_mask == 0) {
      return;
    }
    const auto target = mark_line.find(" -j MARK ");
    if (target == std::string::npos) {
      return;
    }
    mark_line.replace(
        target, mark_line.size() - target,
        keen_pbr3::format(" -j CONNMARK --save-mark --mask {:#x}\n",
                          prefilter.conntrack_mark_mask));
    lines.push_back(std::move(mark_line));
  };
  for (const auto proto : expand_l4_protos_for_iptables(pr.criteria)) {
    const auto port_fragments = build_proto_port_fragments(
        proto, pr.criteria.src_port, pr.criteria.dst_port,
        pr.criteria.negate_src_port, pr.criteria.negate_dst_port);

    for (const auto &pp : port_fragments) {
      for (const auto &iface_frag : iface_frags) {
        if (!pr.criteria.dst_set_name.has_value()) {
          if (pr.action == PendingRule::Mark) {
            const std::string mark_target = keen_pbr3::format(
                "-j MARK --set-xmark {:#x}/{:#x}", pr.fwmark, pr.fwmark_mask);
            append_mark_and_save(keen_pbr3::format("-A {}{}{}{}{} {}\n", chain,
                                                   iface_frag, addr_frag,
                                                   dscp_frag, pp, mark_target));
            lines.push_back(keen_pbr3::format("-A {}{}{}{}{} -j RETURN\n",
                                              chain, iface_frag, addr_frag,
                                              dscp_frag, pp));
          } else if (pr.action == PendingRule::Drop) {
            lines.push_back(keen_pbr3::format("-A {}{}{}{}{} -j DROP\n", chain,
                                              iface_frag, addr_frag, dscp_frag,
                                              pp));
          } else {
            lines.push_back(keen_pbr3::format("-A {}{}{}{}{} -j RETURN\n",
                                              chain, iface_frag, addr_frag,
                                              dscp_frag, pp));
          }
        } else {
          if (pr.action == PendingRule::Mark) {
            const std::string mark_target = keen_pbr3::format(
                "-j MARK --set-xmark {:#x}/{:#x}", pr.fwmark, pr.fwmark_mask);
            append_mark_and_save(keen_pbr3::format(
                "-A {} -m set --match-set {} dst{}{}{}{} {}\n", chain,
                *pr.criteria.dst_set_name, iface_frag, addr_frag, dscp_frag, pp,
                mark_target));
            lines.push_back(keen_pbr3::format(
                "-A {} -m set --match-set {} dst{}{}{}{} -j RETURN\n", chain,
                *pr.criteria.dst_set_name, iface_frag, addr_frag, dscp_frag,
                pp));
          } else if (pr.action == PendingRule::Drop) {
            lines.push_back(keen_pbr3::format(
                "-A {} -m set --match-set {} dst{}{}{}{} -j DROP\n", chain,
                *pr.criteria.dst_set_name, iface_frag, addr_frag, dscp_frag,
                pp));
          } else {
            lines.push_back(keen_pbr3::format(
                "-A {} -m set --match-set {} dst{}{}{}{} -j RETURN\n", chain,
                *pr.criteria.dst_set_name, iface_frag, addr_frag, dscp_frag,
                pp));
          }
        }
      }
    }
  }

  return lines;
}

std::string
IptablesFirewall::build_ipt_script(bool ipv6,
                                   FirewallSetGeneration target_generation,
                                   const std::vector<PendingRule> &rules,
                                   const FirewallGlobalPrefilter &prefilter) {
  const std::string target = generation_chain(target_generation);
  std::string s = "*mangle\n";
  s += keen_pbr3::format(
      ":{} - [0:0]\n:{}_OUTPUT - [0:0]\n:{} - [0:0]\n"
      "-F {}\n-F {}\n-F {}_OUTPUT\n",
      CHAIN_NAME, CHAIN_NAME, target, target, CHAIN_NAME, CHAIN_NAME);
  s += build_prefilter_lines(prefilter, target, /*allow_conntrack=*/true);
  for (const auto &pr : rules) {
    if (pr.ipv6 != ipv6)
      continue;
    for (auto line : build_rule_lines(pr, prefilter, target,
                                      /*allow_conntrack=*/true)) {
      s += line;
    }
  }
  s += keen_pbr3::format("-A {} -j {}\n-A {}_OUTPUT -j {}\n", CHAIN_NAME,
                         target, CHAIN_NAME, target);
  s += "COMMIT\n";
  return s;
}

std::string IptablesFirewall::build_raw_prerouting_script(
    FirewallSetGeneration target_generation,
    const std::vector<PendingRule> &rules,
    const FirewallGlobalPrefilter &prefilter) {
  const std::string target = target_generation == FirewallSetGeneration::A
                                 ? "KeenPbrRaw_A"
                                 : "KeenPbrRaw_B";
  std::string s = "*raw\n";
  s += keen_pbr3::format(
      ":{} - [0:0]\n:{} - [0:0]\n-F {}\n-F {}\n", RAW_CHAIN_NAME, target,
      target, RAW_CHAIN_NAME);
  s += build_prefilter_lines(prefilter, target, /*allow_conntrack=*/false);
  for (const auto &pr : rules) {
    if (pr.ipv6)
      continue;
    for (const auto &line : build_rule_lines(pr, prefilter, target,
                                             /*allow_conntrack=*/false))
      s += line;
  }
  s += keen_pbr3::format("-A {} -j {}\n", RAW_CHAIN_NAME, target);
  return s + "COMMIT\n";
}

std::string IptablesFirewall::build_output_script(
    FirewallSetGeneration target_generation,
    const std::vector<PendingRule> &rules,
    const FirewallGlobalPrefilter &prefilter) {
  const std::string target = output_generation_chain(target_generation);
  std::string s = "*mangle\n";
  s += keen_pbr3::format(
      ":{} - [0:0]\n:{} - [0:0]\n-F {}\n-F {}\n", OUTPUT_CHAIN_NAME, target,
      target, OUTPUT_CHAIN_NAME);
  // OUTPUT remains mangle-based and retains the existing connmark optimization.
  s += build_prefilter_lines(prefilter, target, /*allow_conntrack=*/true);
  for (const auto &pr : rules) {
    if (pr.ipv6)
      continue;
    for (const auto &line : build_rule_lines(pr, prefilter, target,
                                             /*allow_conntrack=*/true))
      s += line;
  }
  s += keen_pbr3::format("-A {} -j {}\n", OUTPUT_CHAIN_NAME, target);
  return s + "COMMIT\n";
}

void IptablesFirewall::apply(FirewallApplyMode mode) {
  if (!apply_prepared_) {
    throw FirewallError("iptables apply was not prepared");
  }
  apply_prepared_ = false;

  bool effective_ipv6 = ipv6_enabled();
  if (effective_ipv6 && !ipv6_backend_available()) {
    Logger::instance().error("IPv6 iptables backend is unavailable; skipping "
                             "IPv6 firewall state and continuing IPv4-only");
    effective_ipv6 = false;
  }

  // RulesOnly is strictly inspection-only for sets: every referenced static
  // and dnsmasq-owned set must already exist with the exact expected schema.
  // Do this before any rule transaction so a failure can safely fall back to
  // PreserveSets.
  if (mode == FirewallApplyMode::RulesOnly) {
    preflight_reused_set_schemas(effective_ipv6);
    if (!pending_elements_.empty()) {
      throw FirewallRulesOnlyError(
          "RulesOnly received buffered set elements; refusing to modify sets");
    }
  } else if (mode != FirewallApplyMode::Destructive ||
             !clear_dynamic_sets_on_apply()) {
    preflight_dynamic_set_schemas(effective_ipv6);
  }

  if (mode == FirewallApplyMode::Destructive) {
    cleanup_live_impl(!clear_dynamic_sets_on_apply(),
                      /*sweep_live_state=*/true);
  }

  if (mode != FirewallApplyMode::Destructive) {
    ensure_target_generation_inactive(false, target_v4_generation_);
    if (effective_ipv6) {
      ensure_target_generation_inactive(true, target_v6_generation_);
    }
  }

  // Phase 1: populate the inactive static generation. Reusing an A/B slot is
  // safe because every target set is flushed before entries are added; a
  // failed restore can only leave partial data in an unreachable generation.
  if (mode != FirewallApplyMode::RulesOnly) {
    std::string ipset_script;
    std::set<std::string> disabled_ipv6_sets;
    for (const auto &ps : pending_sets_) {
      if (ps.family_str == "inet6" && !effective_ipv6) {
        disabled_ipv6_sets.insert(ps.name);
        continue;
      }
      if (is_dynamic_set_name(ps.name)) {
        // dnsmasq owns these entries. A routine re-apply must neither
        // flush them nor alter their existing contents. Re-declaring with
        // -exist also recreates a set lost during an external firewall flush.
        ipset_script += build_ipset_create_line(ps);
        if (mode == FirewallApplyMode::Destructive &&
            clear_dynamic_sets_on_apply()) {
          ipset_script += keen_pbr3::format("flush {}\n", ps.name);
        }
        continue;
      }
      ipset_script += build_ipset_create_line(ps);
      ipset_script += keen_pbr3::format("flush {}\n", ps.name);
    }
    for (auto &[set_name, buf] : pending_elements_) {
      if (disabled_ipv6_sets.find(set_name) != disabled_ipv6_sets.end()) {
        continue;
      }
      std::string elements = buf.str();
      if (!elements.empty()) {
        ipset_script += elements;
      }
    }
    if (!ipset_script.empty()) {
      pipe_to_cmd({"ipset", "restore", "-exist"}, ipset_script);
    }
  }

  // Phase 2: iptables rules via iptables-restore / ip6tables-restore.
  // Always materialize the KeenPbrTable scaffold for both protocols so
  // diagnostics can verify chain/jump presence even when no rules are needed.
  bool has_v4 = true;
  bool has_v6 = effective_ipv6;
  for (const auto &pr : pending_rules_) {
    if (pr.ipv6)
      has_v6 = true;
    else
      has_v4 = true;
  }

  if (has_v4) {
    if (use_raw_prerouting_) {
      // Publish local OUTPUT first and the primary forwarded-traffic
      // PREROUTING path last. Both slots use the same stable fwmarks.
      pipe_to_cmd({"iptables-restore", "--noflush", "--counters"},
                  build_output_script(target_v4_generation_, pending_rules_,
                                      global_prefilter_));
      pipe_to_cmd({"iptables-restore", "--noflush", "--counters"},
                  build_raw_prerouting_script(target_v4_generation_,
                                              pending_rules_,
                                              global_prefilter_));
    } else {
      pipe_to_cmd({"iptables-restore", "--noflush", "--counters"},
                  build_ipt_script(false, target_v4_generation_, pending_rules_,
                                   global_prefilter_));
    }
    chain_v4_created_ = true;
  }
  if (has_v6) {
    pipe_to_cmd({"ip6tables-restore", "--noflush", "--counters"},
                build_ipt_script(true, target_v6_generation_, pending_rules_,
                                 global_prefilter_));
    chain_v6_created_ = true;
  }

  // Hooks are stable entry points and are reconciled separately so --noflush
  // restores never accumulate duplicate builtin-chain jumps.
  reconcile_hooks(false);
  if (has_v6) {
    reconcile_hooks(true);
  }

  // Commit in-memory generation caches only after every enabled table and hook
  // has converged. A failed second table can be retried forward to this target.
  verify_applied_generation(false, target_v4_generation_);
  if (has_v6) {
    verify_applied_generation(true, target_v6_generation_);
  }
  // Clear pending buffers
  pending_sets_.clear();
  pending_elements_.clear();
  pending_rules_.clear();
}

void IptablesFirewall::cleanup_rules_impl(bool sweep_live_state) {
  auto &log = Logger::instance();
  const bool owned_v4 = chain_v4_created_;
  const bool owned_v6 = chain_v6_created_;

  // Remove jump rules, flush and delete custom chain for IPv4
  if (owned_v4 || sweep_live_state) {
    if (use_raw_prerouting_) {
      log.verbose("iptables cleanup: removing raw PREROUTING chain {}",
                  RAW_CHAIN_NAME);
      remove_all_hooks("iptables", "raw", "PREROUTING", RAW_CHAIN_NAME);
      safe_exec({"iptables", "-t", "raw", "-F", RAW_CHAIN_NAME},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "raw", "-X", RAW_CHAIN_NAME},
                /*suppress_output=*/true);
      remove_all_hooks("iptables", "mangle", "OUTPUT", OUTPUT_CHAIN_NAME);
      safe_exec({"iptables", "-t", "mangle", "-F", OUTPUT_CHAIN_NAME},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "mangle", "-X", OUTPUT_CHAIN_NAME},
                /*suppress_output=*/true);
      for (const char *chain : {"KeenPbrRaw_A", "KeenPbrRaw_B"}) {
        safe_exec({"iptables", "-t", "raw", "-F", chain},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "raw", "-X", chain},
                  /*suppress_output=*/true);
      }
      for (const char *chain :
           {output_generation_chain(FirewallSetGeneration::A),
            output_generation_chain(FirewallSetGeneration::B)}) {
        safe_exec({"iptables", "-t", "mangle", "-F", chain},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-X", chain},
                  /*suppress_output=*/true);
      }
      if (sweep_live_state) {
        remove_all_hooks("iptables", "mangle", "PREROUTING", CHAIN_NAME);
        remove_all_hooks("iptables", "mangle", "OUTPUT",
                         "KeenPbrTable_OUTPUT");
        safe_exec({"iptables", "-t", "mangle", "-F", "KeenPbrTable_OUTPUT"},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-X", "KeenPbrTable_OUTPUT"},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-F", CHAIN_NAME},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-X", CHAIN_NAME},
                  /*suppress_output=*/true);
      }
    } else {
      log.verbose("iptables cleanup: removing IPv4 chain {}", CHAIN_NAME);
      remove_all_hooks("iptables", "mangle", "PREROUTING", CHAIN_NAME);
      remove_all_hooks("iptables", "mangle", "OUTPUT", "KeenPbrTable_OUTPUT");
      safe_exec({"iptables", "-t", "mangle", "-F",
                 std::string(CHAIN_NAME) + "_OUTPUT"},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "mangle", "-X",
                 std::string(CHAIN_NAME) + "_OUTPUT"},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "mangle", "-F", CHAIN_NAME},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "mangle", "-X", CHAIN_NAME},
                /*suppress_output=*/true);
      // A restart may switch from raw mode back to the default. Sweep
      // only our explicitly named raw chains; never flush the raw table.
      if (sweep_live_state) {
        remove_all_hooks("iptables", "raw", "PREROUTING", RAW_CHAIN_NAME);
        safe_exec({"iptables", "-t", "raw", "-F", RAW_CHAIN_NAME},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "raw", "-X", RAW_CHAIN_NAME},
                  /*suppress_output=*/true);
        for (const char *chain : {"KeenPbrRaw_A", "KeenPbrRaw_B"}) {
          safe_exec({"iptables", "-t", "raw", "-F", chain},
                    /*suppress_output=*/true);
          safe_exec({"iptables", "-t", "raw", "-X", chain},
                    /*suppress_output=*/true);
        }
        remove_all_hooks("iptables", "mangle", "OUTPUT", OUTPUT_CHAIN_NAME);
        safe_exec({"iptables", "-t", "mangle", "-F", OUTPUT_CHAIN_NAME},
                  /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-X", OUTPUT_CHAIN_NAME},
                  /*suppress_output=*/true);
        for (const char *chain :
             {output_generation_chain(FirewallSetGeneration::A),
              output_generation_chain(FirewallSetGeneration::B)}) {
          safe_exec({"iptables", "-t", "mangle", "-F", chain},
                    /*suppress_output=*/true);
          safe_exec({"iptables", "-t", "mangle", "-X", chain},
                    /*suppress_output=*/true);
        }
      }
    }
    chain_v4_created_ = false;
  }

  // Same for IPv6
  if (owned_v6 || sweep_live_state) {
    log.verbose("iptables cleanup: removing IPv6 chain {}", CHAIN_NAME);
    remove_all_hooks("ip6tables", "mangle", "PREROUTING", CHAIN_NAME);
    remove_all_hooks("ip6tables", "mangle", "OUTPUT", "KeenPbrTable_OUTPUT");
    safe_exec({"ip6tables", "-t", "mangle", "-F",
               std::string(CHAIN_NAME) + "_OUTPUT"},
              /*suppress_output=*/true);
    safe_exec({"ip6tables", "-t", "mangle", "-X",
               std::string(CHAIN_NAME) + "_OUTPUT"},
              /*suppress_output=*/true);
    safe_exec({"ip6tables", "-t", "mangle", "-F", CHAIN_NAME},
              /*suppress_output=*/true);
    safe_exec({"ip6tables", "-t", "mangle", "-X", CHAIN_NAME},
              /*suppress_output=*/true);
    chain_v6_created_ = false;
  }

  if (sweep_live_state ||
      (!use_raw_prerouting_ && (owned_v4 || owned_v6))) {
    for (const char *chain : {generation_chain(FirewallSetGeneration::A),
                              generation_chain(FirewallSetGeneration::B)}) {
      safe_exec({"iptables", "-t", "mangle", "-F", chain},
                /*suppress_output=*/true);
      safe_exec({"iptables", "-t", "mangle", "-X", chain},
                /*suppress_output=*/true);
      safe_exec({"ip6tables", "-t", "mangle", "-F", chain},
                /*suppress_output=*/true);
      safe_exec({"ip6tables", "-t", "mangle", "-X", chain},
                /*suppress_output=*/true);
    }
  }
  if (sweep_live_state) {
    cleanup_legacy_generation_chains("iptables");
    cleanup_legacy_generation_chains("ip6tables");
  }
}

void IptablesFirewall::cleanup_legacy_generation_chains(const char *command) {
  const auto result = safe_exec_capture({command, "-t", "mangle", "-S"},
                                        /*suppress_stderr=*/true);
  if (result.exit_code != 0) {
    return;
  }

  std::istringstream input(result.stdout_output);
  std::string line;
  constexpr std::string_view prefix = "-N KeenPbrTable_";
  while (std::getline(input, line)) {
    if (line.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::string chain = line.substr(prefix.size());
    if (chain.empty() ||
        !std::all_of(chain.begin(), chain.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      continue;
    }
    safe_exec({command, "-t", "mangle", "-F", chain}, /*suppress_output=*/true);
    safe_exec({command, "-t", "mangle", "-X", chain}, /*suppress_output=*/true);
  }
}

void IptablesFirewall::cleanup_saved_sets(bool preserve_dynamic_sets) {
  const auto result =
      safe_exec_capture({"ipset", "save"}, /*suppress_stderr=*/true);
  if (result.exit_code != 0) {
    return;
  }

  std::istringstream input(result.stdout_output);
  std::string verb;
  std::string name;
  std::string rest;
  while (input >> verb >> name) {
    std::getline(input, rest);
    if (verb != "create") {
      continue;
    }
    const bool dynamic = is_dynamic_set_name(name);
    const bool managed_static =
        name.rfind("kpbr4_", 0) == 0 || name.rfind("kpbr6_", 0) == 0 ||
        name.rfind("kpbr4s_", 0) == 0 || name.rfind("kpbr6s_", 0) == 0 ||
        name.rfind("kpbr4S_", 0) == 0 || name.rfind("kpbr6S_", 0) == 0;
    if (!managed_static && !dynamic) {
      continue;
    }
    if (dynamic && preserve_dynamic_sets) {
      continue;
    }
    safe_exec({"ipset", "flush", name}, /*suppress_output=*/true);
    safe_exec({"ipset", "destroy", name}, /*suppress_output=*/true);
  }
}

void IptablesFirewall::cleanup_live_impl(bool preserve_dynamic_sets,
                                         bool sweep_live_state) {
  auto &log = Logger::instance();

  cleanup_rules_impl(sweep_live_state);

  // Destroy all created ipsets
  for (const auto &[name, _] : created_sets_) {
    if (preserve_dynamic_sets && is_dynamic_set_name(name)) {
      continue;
    }
    log.verbose("iptables cleanup: destroying ipset {}", name);
    safe_exec({"ipset", "flush", name}, /*suppress_output=*/true);
    safe_exec({"ipset", "destroy", name}, /*suppress_output=*/true);
  }
  if (sweep_live_state) {
    cleanup_saved_sets(preserve_dynamic_sets);
  }
}

void IptablesFirewall::cleanup_impl() {
  // Explicit cleanup is authoritative and must work after restart or a
  // partially failed apply, when the ownership booleans are necessarily stale.
  cleanup_live_impl(/*preserve_dynamic_sets=*/false,
                    /*sweep_live_state=*/true);

  created_sets_.clear();

  pending_sets_.clear();
  pending_elements_.clear();
  pending_rules_.clear();
}

void IptablesFirewall::cleanup() { cleanup_impl(); }

FirewallBackend IptablesFirewall::backend() const {
  return FirewallBackend::iptables;
}

std::unique_ptr<Firewall> create_iptables_firewall(bool use_raw_prerouting) {
  return std::make_unique<IptablesFirewall>(use_raw_prerouting);
}

} // namespace keen_pbr3
