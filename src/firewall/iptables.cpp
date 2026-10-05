#include "iptables.hpp"
#include "firewall_lowering.hpp"
#include "firewall_plan.hpp"
#include "firewall_rule.hpp"
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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>

namespace keen_pbr3 {

namespace {

bool cleanup_command_reports_absence(const ExecCaptureResult &result) {
  if (result.exit_code == 0 && !result.truncated && !result.timed_out) {
    return true;
  }
  if (result.truncated || result.timed_out) {
    return false;
  }
  std::string output = result.stdout_output;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return output.find("no chain") != std::string::npos ||
         output.find("no such") != std::string::npos ||
         output.find("does not exist") != std::string::npos ||
         output.find("cannot be found") != std::string::npos;
}

ExecCaptureResult run_cleanup_command(const std::vector<std::string> &args) {
  const auto result = safe_exec_capture(args, /*suppress_stderr=*/false,
                                        /*max_bytes=*/0,
                                        /*merge_stderr=*/true);
  if (!cleanup_command_reports_absence(result)) {
    throw FirewallError(keen_pbr3::format(
        "firewall cleanup command failed: {} (status {})",
        safe_exec_command_string(args), result.exit_code));
  }
  return result;
}

} // namespace

IptablesFirewall::IptablesFirewall(RawPreroutingMode raw_prerouting)
    : raw_prerouting_(raw_prerouting) {
  if (raw_prerouting_.ipv4) {
    validate_raw_prerouting_capability(false);
  }
  if (raw_prerouting_.ipv6) {
    validate_raw_prerouting_capability(true);
  }
}

const char *IptablesFirewall::prerouting_table_name(bool ipv6) const {
  return uses_raw_prerouting(ipv6) ? "raw" : "mangle";
}

const char *IptablesFirewall::prerouting_chain_name(bool ipv6) const {
  return iptables_prerouting_chain_name(uses_raw_prerouting(ipv6));
}

void IptablesFirewall::validate_raw_prerouting_capability(bool ipv6) const {
  const char *family_label = ipv6 ? "IPv6" : "IPv4";
  const char *command = ipv6 ? "ip6tables" : "iptables";
  const char *registry_path =
      ipv6 ? "/proc/net/ip6_tables_names" : "/proc/net/ip_tables_names";
  const char *module_name = ipv6 ? "ip6table_raw.ko" : "iptable_raw.ko";
  std::ifstream tables(registry_path);
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
          ? std::string("/lib/modules/") + uts.release + "/" + module_name
          : std::string("/lib/modules/$(uname -r)/") + module_name;
  if (!raw_present) {
    throw FirewallError(
        std::string("--use-raw") + (ipv6 ? "6" : "") +
        "-prerouting requested for " + family_label +
        ", but raw is absent from " + registry_path + " (expected module " +
        module + "); no fallback to mangle PREROUTING was performed");
  }
  const int probe = safe_exec({command, "-t", "raw", "-S"},
                              /*suppress_output=*/true);
  if (probe != 0) {
    throw FirewallError(
        std::string("--use-raw") + (ipv6 ? "6" : "") +
        "-prerouting requested for " + family_label +
        ", but " + command + " -t raw -S failed after raw was registered in " +
        registry_path + "; no fallback to mangle PREROUTING was performed");
  }
}

void IptablesFirewall::prepare_apply(FirewallApplyMode mode) {
  pending_sets_.clear();
  pending_elements_.clear();
  pending_ruleset_ = {};
  prepared_mode_ = mode;
  apply_prepared_ = false;

  // Probe the optional ownership-match extension before any mode can clean
  // live chains, mutate ipsets, or publish a restore transaction.  IPv4 is
  // always an active backend; IPv6 only needs probing when it is enabled and
  // its backend is available.  Unsupported frontends simply omit comments.
  comment_v4_supported_ = comments_override_.has_value()
                              ? *comments_override_
                              : probe_xt_comment(false);
  comment_v6_supported_ =
      comments_override_.has_value()
          ? *comments_override_
          : (!ipv6_enabled() || !ipv6_backend_available() ||
             probe_xt_comment(true));

  // RulesOnly preparation is deliberately inspection-only: it reuses the live
  // rules and sets, so the live PREROUTING chain must exist for each active
  // family.
  if (mode == FirewallApplyMode::RulesOnly) {
    const auto require_chain = [this](bool ipv6) {
      const char *label = ipv6 ? "IPv6" : "IPv4";
      bool present = false;
      try {
        present = live_prerouting_chain_present(ipv6);
      } catch (const FirewallError &error) {
        throw FirewallRulesOnlyError(std::string("cannot inspect live ") +
                                     label + " firewall rules: " +
                                     error.what());
      }
      if (!present) {
        throw FirewallRulesOnlyError(
            std::string("cannot reuse ") + label +
            " firewall rules: live PREROUTING chain is missing");
      }
    };
    require_chain(false);
    if (ipv6_enabled() && ipv6_backend_available()) {
      require_chain(true);
    }
  }
  apply_prepared_ = true;
}

void IptablesFirewall::create_ipset(const std::string &set_name, int family,
                                    uint32_t timeout) {
  PendingSet ps;
  ps.name = set_name;
  ps.family_str = (family == AF_INET6) ? "inet6" : "inet";
  ps.timeout = timeout;
  ps.hashsize = ipset_hashsize();
  ps.maxelem = ipset_maxelem();
  const auto existing = std::find_if(pending_sets_.begin(), pending_sets_.end(),
                                     [&set_name](const PendingSet &pending) {
                                       return pending.name == set_name;
                                     });
  if (existing == pending_sets_.end()) {
    pending_sets_.push_back(std::move(ps));
  } else if (existing->family_str != ps.family_str ||
             existing->timeout != ps.timeout ||
             existing->hashsize != ps.hashsize ||
             existing->maxelem != ps.maxelem) {
    throw FirewallError("conflicting ipset declaration for " + set_name);
  }
  created_sets_[set_name] = family;
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
  std::string line = keen_pbr3::format("create {} hash:net family {}", ps.name,
                                       ps.family_str);
  if (ps.hashsize.has_value()) {
    line += keen_pbr3::format(" hashsize {}", *ps.hashsize);
  }
  if (ps.maxelem.has_value()) {
    line += keen_pbr3::format(" maxelem {}", *ps.maxelem);
  }
  // Dynamic sets are filled by the daemon with per-element timeouts, so they
  // always carry timeout support. `timeout 0` keeps the set-wide default
  // permanent while still accepting per-element `timeout N` (verified against
  // the kernel), so the set-wide value may be 0.
  if (ps.timeout > 0 || is_dynamic_set_name(ps.name)) {
    line += keen_pbr3::format(" timeout {}", ps.timeout);
  }
  return line + " -exist\n";
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
  auto *hashsize_node = header->first_node("hashsize");
  auto *maxelem_node = header->first_node("maxelem");
  if (family == nullptr || header->last_node("family") != family ||
      hashsize_node == nullptr ||
      hashsize_node->next_sibling("hashsize") != nullptr ||
      maxelem_node == nullptr ||
      maxelem_node->next_sibling("maxelem") != nullptr ||
      (timeout_node != nullptr &&
       header->last_node("timeout") != timeout_node)) {
    return false;
  }

  const std::string_view live_name(name->value(), name->value_size());
  const std::string_view live_type(type->value(), type->value_size());
  const std::string_view live_family(family->value(), family->value_size());
  uint32_t live_timeout = 0;
  if (timeout_node == nullptr && is_dynamic_set_name(expected.name)) {
    // Dynamic sets must support per-element timeouts (`timeout 0` is enough).
    return false;
  }
  if (timeout_node != nullptr) {
    const char *begin = timeout_node->value();
    const char *end = begin + timeout_node->value_size();
    const auto parsed = std::from_chars(begin, end, live_timeout);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return false;
    }
  }
  uint32_t live_hashsize = 0;
  {
    const char *begin = hashsize_node->value();
    const char *end = begin + hashsize_node->value_size();
    const auto parsed = std::from_chars(begin, end, live_hashsize);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return false;
    }
  }
  uint32_t live_maxelem = 0;
  {
    const char *begin = maxelem_node->value();
    const char *end = begin + maxelem_node->value_size();
    const auto parsed = std::from_chars(begin, end, live_maxelem);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return false;
    }
  }
  const auto requested_hashsize =
      normalize_ipset_hashsize(expected.hashsize.value_or(1024));
  if (!requested_hashsize.has_value() ||
      live_hashsize < *requested_hashsize ||
      live_maxelem != expected.maxelem.value_or(65536)) {
    return false;
  }
  return live_name == expected.name && live_type == "hash:net" &&
         live_family == expected.family_str &&
         live_timeout == expected.timeout;
}

std::optional<std::set<std::string>> IptablesFirewall::list_live_ipset_names() {
  const auto names =
      safe_exec_capture({"ipset", "list", "-n"}, /*suppress_stderr=*/true);
  if (names.exit_code != 0 || names.truncated || names.timed_out) {
    return std::nullopt;
  }
  std::set<std::string> result;
  std::size_t pos = 0;
  const std::string &text = names.stdout_output;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    if (end > pos) result.emplace(text, pos, end - pos);
    pos = end + 1;
  }
  return result;
}

IptablesFirewall::OwnedSetKind
IptablesFirewall::classify_owned_static_set(const std::string &name) {
  // "kpbr" <4|6> <'' | t | s | S> "_" <tag>
  if (name.size() < 7 || name.compare(0, 4, "kpbr") != 0 ||
      (name[4] != '4' && name[4] != '6')) {
    return OwnedSetKind::None;
  }
  OwnedSetKind kind;
  switch (name[5]) {
  case '_': kind = OwnedSetKind::Stable; break;
  case 't': kind = OwnedSetKind::Temp; break;
  case 's':
  case 'S': kind = OwnedSetKind::Legacy; break;
  default: return OwnedSetKind::None;
  }
  const std::size_t tag_begin = kind == OwnedSetKind::Stable ? 6 : 7;
  if (kind != OwnedSetKind::Stable && name[6] != '_') {
    return OwnedSetKind::None;
  }
  const std::size_t tag_len = name.size() - tag_begin;
  if (tag_len == 0 || tag_len > 24 || name[tag_begin] < 'a' ||
      name[tag_begin] > 'z') {
    return OwnedSetKind::None;
  }
  for (std::size_t i = tag_begin + 1; i < name.size(); ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return OwnedSetKind::None;
    }
  }
  return kind;
}

std::optional<std::string>
IptablesFirewall::temp_set_name(const std::string &stable) {
  if (classify_owned_static_set(stable) != OwnedSetKind::Stable) {
    return std::nullopt;
  }
  std::string temp = stable;
  temp.insert(5, 1, 't');
  return temp;
}

void IptablesFirewall::append_retargeted_elements(std::string &out,
                                                  const std::string &elements,
                                                  const std::string &from,
                                                  const std::string &to) {
  const std::string old_prefix = "add " + from + " ";
  const std::string new_prefix = "add " + to + " ";
  out.reserve(out.size() + elements.size() + elements.size() / 16U);
  std::size_t pos = 0;
  while (pos < elements.size()) {
    std::size_t end = elements.find('\n', pos);
    end = end == std::string::npos ? elements.size() : end + 1;
    if (elements.compare(pos, old_prefix.size(), old_prefix) == 0) {
      out += new_prefix;
      out.append(elements, pos + old_prefix.size(),
                 end - pos - old_prefix.size());
    } else {
      out.append(elements, pos, end - pos);
    }
    pos = end;
  }
}

bool IptablesFirewall::destroy_set_best_effort(const std::string &name) {
  const auto result = safe_exec_capture({"ipset", "destroy", name},
                                        /*suppress_stderr=*/false,
                                        /*max_bytes=*/0,
                                        /*merge_stderr=*/true);
  if (result.exit_code == 0 && !result.truncated && !result.timed_out) {
    return true;
  }
  std::string output = result.stdout_output;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (output.find("does not exist") != std::string::npos ||
      output.find("no such") != std::string::npos) {
    return true;
  }
  Logger::instance().warn("ipset destroy {} failed (status {}): {}", name,
                          result.exit_code, result.stdout_output);
  return false;
}

void IptablesFirewall::destroy_unreferenced_static_sets(
    const std::set<std::string> &live, const std::set<std::string> &keep) {
  for (const auto &name : live) {
    const OwnedSetKind kind = classify_owned_static_set(name);
    if (kind == OwnedSetKind::None ||
        (kind == OwnedSetKind::Stable && keep.count(name) != 0)) {
      continue;
    }
    Logger::instance().verbose("iptables apply: destroying unreferenced ipset {}",
                               name);
    if (destroy_set_best_effort(name)) {
      created_sets_.erase(name);
    }
  }
}

std::optional<std::string>
IptablesFirewall::find_incompatible_dynamic_set_schema(
    bool effective_ipv6) const {
  const bool has_dynamic = std::any_of(
      pending_sets_.begin(), pending_sets_.end(), [&](const PendingSet &set) {
        return is_dynamic_set_name(set.name) &&
               (set.family_str != "inet6" || effective_ipv6);
      });
  if (!has_dynamic) {
    return std::nullopt;
  }

  const auto live_names = list_live_ipset_names();
  if (!live_names.has_value()) {
    throw FirewallError("failed to inspect dynamic ipset schemas");
  }
  for (const auto &set : pending_sets_) {
    if (!is_dynamic_set_name(set.name) ||
        (set.family_str == "inet6" && !effective_ipv6)) {
      continue;
    }
    if (live_names->find(set.name) == live_names->end()) {
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
      return set.name;
    }
  }
  return std::nullopt;
}

void IptablesFirewall::preflight_dynamic_set_schemas(
    bool effective_ipv6) const {
  const auto incompatible =
      find_incompatible_dynamic_set_schema(effective_ipv6);
  if (incompatible.has_value()) {
    throw FirewallError(
        "incompatible existing dynamic ipset family, type, timeout, or "
        "capacity schema for " +
        *incompatible);
  }
}

void IptablesFirewall::preflight_reused_set_schemas(
    bool effective_ipv6) const {
  const auto live_names = list_live_ipset_names();
  if (!live_names.has_value()) {
    throw FirewallRulesOnlyError(
        "failed to inspect required reused ipset names");
  }

  for (const auto &chain : pending_ruleset_.chains) {
    const bool ipv6 = chain.id.family == FirewallFamily::ipv6;
    for (const auto &rule : chain.rules) {
      for (const auto &match : rule.matches) {
        const auto *set_match = std::get_if<SetMatch>(&match);
        if (set_match == nullptr) {
          continue;
        }
        const auto expected = std::find_if(
            pending_sets_.begin(), pending_sets_.end(),
            [&](const PendingSet &set) {
              return set.name == set_match->name &&
                     ((ipv6 && set.family_str == "inet6") ||
                      (!ipv6 && set.family_str == "inet"));
            });
        if (expected == pending_sets_.end()) {
          throw FirewallRulesOnlyError(
              "required reused ipset " + set_match->name +
              " has no compatible declaration for the packet family");
        }
      }
    }
  }

  for (const auto &set : pending_sets_) {
    if (set.family_str == "inet6" && !effective_ipv6) {
      continue;
    }
    if (live_names->find(set.name) == live_names->end()) {
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
          " has incompatible family, type, timeout, or capacity schema");
    }
  }
}

bool IptablesFirewall::ipv6_backend_available() const {
  if (ipv6_backend_override_.has_value()) {
    return *ipv6_backend_override_;
  }
  return iptables_ipv6_supported();
}

bool IptablesFirewall::probe_xt_comment(bool ipv6) const {
  const char *registration_path =
      ipv6 ? "/proc/net/ip6_tables_matches" : "/proc/net/ip_tables_matches";
  return probe_xt_comment_from_registration(ipv6, registration_path);
}

bool IptablesFirewall::has_xt_comment_registration(
    const std::string &contents) {
  // /proc/net/*_tables_matches is a whitespace-separated registration list.
  // Match a complete token so similarly named extensions cannot accidentally
  // enable comments (for example, "xt_comment_extra").
  std::istringstream lines(contents);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string token;
    while (fields >> token) {
      if (token == "comment") {
        return true;
      }
    }
  }
  return false;
}

bool IptablesFirewall::probe_xt_comment_from_registration(
    bool ipv6, const std::string &registration_path) const {
  // The proc registration is the only read-only evidence that the kernel has
  // the match registered.  Do not create a temporary rule: restore --test
  // does not commit and, on legacy backends, may skip the kernel commit path.
  std::ifstream registration(registration_path);
  if (!registration) {
    Logger::instance().warn(
        "{} is unavailable; omitting ownership comments for {} rules",
        registration_path, ipv6 ? "IPv6" : "IPv4");
    return false;
  }
  std::ostringstream contents;
  contents << registration.rdbuf();
  if (registration.bad() || (registration.fail() && !registration.eof())) {
    Logger::instance().warn(
        "failed to read {}; omitting ownership comments for {} rules",
        registration_path, ipv6 ? "IPv6" : "IPv4");
    return false;
  }
  if (!has_xt_comment_registration(contents.str())) {
    Logger::instance().warn(
        "{} has no exact xt_comment registration; omitting ownership "
        "comments for {} rules",
        registration_path, ipv6 ? "IPv6" : "IPv4");
    return false;
  }

  // This is a userspace restore grammar/argument check only.  Passing a
  // failing modprobe command prevents an unavailable extension from being
  // loaded as a side effect; the proc registration check above remains the
  // kernel capability gate.  Both checks run before any apply mutation.
  const char *command = ipv6 ? "ip6tables-restore" : "iptables-restore";
  const std::string probe_script =
      "*mangle\n"
      ":KpbrXtCommentProbe - [0:0]\n"
      "-A KpbrXtCommentProbe -m comment --comment kpbr:v1:probe -j RETURN\n"
      "COMMIT\n";
  const int status = safe_exec_pipe_stdin(
      {command, "--test", "--noflush", "--modprobe=/bin/false"},
      probe_script);
  if (status == 0) {
    return true;
  }
  Logger::instance().warn(
      "{} failed the read-only xt_comment restore grammar preflight; "
      "omitting ownership comments for {} rules",
      command, ipv6 ? "IPv6" : "IPv4");
  return false;
}

std::string IptablesFirewall::capture_table_dump(bool ipv6, const char *table) {
  const char *command = ipv6 ? "ip6tables" : "iptables";
  const auto result = safe_exec_capture({command, "-t", table, "-S"},
                                        /*suppress_stderr=*/true);
  if (result.exit_code != 0 || result.truncated || result.timed_out) {
    throw FirewallError(keen_pbr3::format("failed to inspect live {} {} table",
                                          command, table));
  }
  return result.stdout_output;
}

namespace {

// One jump rule (`-A <source> ... -j|-g <target>`) of an `iptables -S` dump.
struct DumpJump {
  std::string source;
  std::string target;
  // The rule is exactly `-A <source> -j <target>` (a plain hook).
  bool exact{false};
  // 0-based index among all `-A <source>` rules of the dump.
  std::size_t index{0};
};

// Tokenize one `-S` line on spaces.  `-S` output never needs quote handling
// for the tokens inspected here (chain names, -j/-g targets, set names).
std::vector<std::string_view> split_dump_line(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t pos = 0;
  while (pos < line.size()) {
    while (pos < line.size() && line[pos] == ' ') ++pos;
    const std::size_t start = pos;
    while (pos < line.size() && line[pos] != ' ') ++pos;
    if (pos > start) tokens.push_back(line.substr(start, pos - start));
  }
  return tokens;
}

// Parsed view of `iptables -t <table> -S` needed to plan a restore.
struct ObservedTable {
  std::set<std::string> chains;   // user-defined chains (`-N`)
  std::set<std::string> builtins; // builtin chains (`-P`)
  std::vector<DumpJump> jumps;
};

ObservedTable parse_observed_table(const std::string &dump) {
  ObservedTable table;
  std::map<std::string, std::size_t> rules_per_chain;
  std::size_t pos = 0;
  while (pos < dump.size()) {
    std::size_t end = dump.find('\n', pos);
    if (end == std::string::npos) end = dump.size();
    const std::string_view line(dump.data() + pos, end - pos);
    pos = end + 1;
    const auto tokens = split_dump_line(line);
    if (tokens.size() < 2) continue;
    if (tokens[0] == "-N") {
      table.chains.emplace(tokens[1]);
    } else if (tokens[0] == "-P") {
      table.builtins.emplace(tokens[1]);
    } else if (tokens[0] == "-A") {
      const std::string source(tokens[1]);
      const std::size_t rule_index = rules_per_chain[source]++;
      for (std::size_t i = 2; i < tokens.size(); ++i) {
        if (tokens[i] == "-j" || tokens[i] == "-g") {
          if (i + 1 < tokens.size()) {
            DumpJump jump;
            jump.source = source;
            jump.target.assign(tokens[i + 1]);
            jump.exact = tokens.size() == 4 && i == 2;
            jump.index = rule_index;
            table.jumps.push_back(std::move(jump));
          }
          ++i;
        }
      }
    }
  }
  return table;
}

// Retired A/B-layout chains: flushed and deleted by the restore of their
// table once nothing references them any more.
constexpr const char *kLegacyChains[] = {
    "KeenPbrTable_OUTPUT", "KeenPbrTable_A", "KeenPbrTable_B",
    "KeenPbrRaw_A",        "KeenPbrRaw_B",   "KeenPbrOutput_A",
    "KeenPbrOutput_B",
    // Interception chains are retired the same way whenever the plan no
    // longer carries them (they are skipped while declared by the commit).
    "KeenPbrDnsHold",      "KeenPbrSniff",   "KeenPbrSniffOut"};

} // namespace

bool IptablesFirewall::live_prerouting_chain_present(bool ipv6) const {
  const auto table = parse_observed_table(
      capture_table_dump(ipv6, prerouting_table_name(ipv6)));
  return table.chains.count(prerouting_chain_name(ipv6)) != 0;
}

size_t IptablesFirewall::count_exact_jump(const std::string &rules,
                                          const std::string &source_chain,
                                          const std::string &target_chain) {
  const std::string expected = "-A " + source_chain + " -j " + target_chain;
  size_t count = 0;
  std::size_t pos = 0;
  while (pos < rules.size()) {
    std::size_t end = rules.find('\n', pos);
    if (end == std::string::npos) end = rules.size();
    if (rules.compare(pos, end - pos, expected) == 0) {
      ++count;
    }
    pos = end + 1;
  }
  return count;
}

bool IptablesFirewall::first_rule_is_jump(const std::string &rules,
                                          const std::string &source_chain,
                                          const std::string &target_chain) {
  const std::string prefix = "-A " + source_chain + " ";
  std::size_t pos = 0;
  while (pos < rules.size()) {
    std::size_t end = rules.find('\n', pos);
    if (end == std::string::npos) end = rules.size();
    if (rules.compare(pos, prefix.size(), prefix) == 0) {
      return rules.compare(pos, end - pos,
                           "-A " + source_chain + " -j " + target_chain) == 0;
    }
    pos = end + 1;
  }
  return false;
}

void IptablesFirewall::remove_all_hooks(const char *command, const char *table,
                                        const char *builtin_chain,
                                        const char *target_chain) {
  const auto result = run_cleanup_command(
      {command, "-t", table, "-S", builtin_chain});
  if (result.truncated || result.timed_out) {
    return;
  }
  const size_t observed =
      count_exact_jump(result.stdout_output, builtin_chain, target_chain);
  for (size_t i = 0; i < observed; ++i) {
    run_cleanup_command(
        {command, "-t", table, "-D", builtin_chain, "-j", target_chain});
  }
}

void IptablesFirewall::verify_applied_hooks(bool ipv6) const {
  const char *command = ipv6 ? "ip6tables" : "iptables";
  const auto prerouting_hook = safe_exec_capture(
      {command, "-t", prerouting_table_name(ipv6), "-S", "PREROUTING"},
      /*suppress_stderr=*/true);
  const auto output_hook =
      safe_exec_capture({command, "-t", "mangle", "-S", "OUTPUT"},
                        /*suppress_stderr=*/true);
  if (prerouting_hook.exit_code != 0 || output_hook.exit_code != 0 ||
      count_exact_jump(prerouting_hook.stdout_output, "PREROUTING",
                       prerouting_chain_name(ipv6)) != 1 ||
      count_exact_jump(output_hook.stdout_output, "OUTPUT",
                       iptables_output_chain_name()) != 1) {
    throw FirewallError("iptables builtin hook verification failed");
  }

  // Interception chains present in the applied ruleset: exactly one jump,
  // and it is the first rule of its builtin chain.
  const auto family = ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
  const auto has_chain = [&](const char *name) {
    return pending_ruleset_.find(iptables_physical_chain_id(
               name, PhysicalTable::mangle, family)) != nullptr;
  };
  const auto verify_pinned = [&](const char *builtin, const char *target) {
    const auto dump = safe_exec_capture(
        {command, "-t", "mangle", "-S", builtin}, /*suppress_stderr=*/true);
    if (dump.exit_code != 0 ||
        count_exact_jump(dump.stdout_output, builtin, target) != 1 ||
        !first_rule_is_jump(dump.stdout_output, builtin, target)) {
      throw FirewallError("iptables builtin hook verification failed");
    }
  };
  if (has_chain(iptables_dns_hold_chain_name())) {
    verify_pinned("POSTROUTING", iptables_dns_hold_chain_name());
  }
  if (has_chain(iptables_sniff_chain_name())) {
    verify_pinned("FORWARD", iptables_sniff_chain_name());
    if (pending_ruleset_
            .find(iptables_physical_chain_id(iptables_sniff_chain_name(),
                                             PhysicalTable::mangle, family))
            ->output_hook) {
      verify_pinned("OUTPUT", iptables_sniff_chain_name());
    }
  }
  if (has_chain(iptables_sniff_out_chain_name())) {
    verify_pinned("OUTPUT", iptables_sniff_out_chain_name());
  }
}

namespace {

// Emission order of the matches of one rule.  Rules are held in canonical
// kind order; iptables-restore only needs `-p` before the port options and a
// module before its options.
int iptables_match_rank(const PhysicalMatch &match) {
  if (std::holds_alternative<SetMatch>(match)) return 0;
  if (std::holds_alternative<IifMatch>(match)) return 1;
  if (std::holds_alternative<OifMatch>(match)) return 1;
  if (std::holds_alternative<AddrMatch>(match)) return 2;
  if (std::holds_alternative<DscpMatch>(match)) return 3;
  if (std::holds_alternative<ProtoMatch>(match)) return 4;
  if (std::holds_alternative<PortMatch>(match)) return 5;
  // ctstate before ctdir: the two share one `-m conntrack` when adjacent.
  if (std::holds_alternative<CtStateMatch>(match)) return 6;
  if (std::holds_alternative<CtDirMatch>(match)) return 7;
  return 8;
}

void append_hex(std::string &out, uint32_t value) {
  char buffer[16];
  const int length = std::snprintf(buffer, sizeof(buffer), "0x%x", value);
  out.append(buffer, static_cast<std::size_t>(length));
}

void append_port_ranges(std::string &out, const std::vector<PortRange> &ranges) {
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    if (index != 0) out.push_back(',');
    out += std::to_string(ranges[index].from);
    if (ranges[index].from != ranges[index].to) {
      out.push_back(':');
      out += std::to_string(ranges[index].to);
    }
  }
}

void append_ct_states(std::string &out, uint8_t states) {
  struct Named {
    uint8_t bit;
    const char *name;
  };
  static constexpr Named kNames[] = {
      {ct_new, "NEW"},         {ct_established, "ESTABLISHED"},
      {ct_related, "RELATED"}, {ct_invalid, "INVALID"},
      {ct_untracked, "UNTRACKED"}, {ct_snat, "SNAT"}, {ct_dnat, "DNAT"}};
  bool first = true;
  for (const auto &named : kNames) {
    if ((states & named.bit) == 0) continue;
    if (!first) out.push_back(',');
    out += named.name;
    first = false;
  }
}

bool is_conntrack_match(const PhysicalMatch &match) {
  return std::holds_alternative<CtStateMatch>(match) ||
         std::holds_alternative<CtDirMatch>(match);
}

// `after_conntrack`: the previous rendered match was a conntrack option, so
// this one continues the same `-m conntrack` instead of opening another.
void append_iptables_match(std::string &out, const PhysicalMatch &match,
                           bool after_conntrack = false) {
  if (const auto *set = std::get_if<SetMatch>(&match)) {
    out += set->negate ? " -m set ! --match-set " : " -m set --match-set ";
    out += set->name;
    out += set->dir == PhysicalDir::src ? " src" : " dst";
  } else if (const auto *iif = std::get_if<IifMatch>(&match)) {
    if (iif->names.size() != 1U) {
      throw FirewallError(
          "iptables cannot express an interface list in one rule");
    }
    out += iif->negate ? " ! -i " : " -i ";
    out += iif->names.front();
  } else if (const auto *oif = std::get_if<OifMatch>(&match)) {
    if (oif->names.size() != 1U) {
      throw FirewallError(
          "iptables cannot express an interface list in one rule");
    }
    out += oif->negate ? " ! -o " : " -o ";
    out += oif->names.front();
  } else if (const auto *type = std::get_if<AddrTypeMatch>(&match)) {
    // One type per rule keeps old xt_addrtype revisions happy.
    if (type->types != addr_broadcast && type->types != addr_multicast) {
      throw FirewallError(
          "iptables cannot express a destination type list in one rule");
    }
    out += type->types == addr_broadcast
               ? " -m addrtype --dst-type BROADCAST"
               : " -m addrtype --dst-type MULTICAST";
  } else if (const auto *addr = std::get_if<AddrMatch>(&match)) {
    out += addr->negate ? " ! " : " ";
    out += addr->dir == PhysicalDir::src ? "-s " : "-d ";
    for (std::size_t index = 0; index < addr->cidrs.size(); ++index) {
      if (index != 0) out.push_back(',');
      out += addr->cidrs[index];
    }
  } else if (const auto *dscp = std::get_if<DscpMatch>(&match)) {
    out += " -m dscp --dscp ";
    out += std::to_string(static_cast<int>(dscp->value));
  } else if (const auto *proto = std::get_if<ProtoMatch>(&match)) {
    out += " -p ";
    out += l4_proto_name(proto->proto);
  } else if (const auto *port = std::get_if<PortMatch>(&match)) {
    const bool list = port->ranges.size() > 1U;
    const bool source = port->dir == PhysicalDir::src;
    if (list) {
      out += port->negate ? " -m multiport !" : " -m multiport";
      out += source ? " --sports " : " --dports ";
    } else {
      out += port->negate ? " !" : "";
      out += source ? " --sport " : " --dport ";
    }
    append_port_ranges(out, port->ranges);
  } else if (const auto *dir = std::get_if<CtDirMatch>(&match)) {
    out += after_conntrack ? "" : " -m conntrack";
    out += dir->original ? " --ctdir ORIGINAL" : " --ctdir REPLY";
  } else if (const auto *state = std::get_if<CtStateMatch>(&match)) {
    out += after_conntrack ? "" : " -m conntrack";
    out += state->negate ? " ! --ctstate " : " --ctstate ";
    append_ct_states(out, state->states);
  } else if (const auto *bytes = std::get_if<ConnbytesMatch>(&match)) {
    out += " -m connbytes --connbytes ";
    out += std::to_string(bytes->from);
    out += ":";
    out += std::to_string(bytes->to);
    out += " --connbytes-dir ";
    out += bytes->dir == ConnbytesDir::original ? "original"
        : bytes->dir == ConnbytesDir::reply ? "reply" : "both";
    out += " --connbytes-mode ";
    out += bytes->mode == ConnbytesMode::packets ? "packets" : "bytes";
  } else if (const auto *mark = std::get_if<MarkMatch>(&match)) {
    if (mark->values.size() != 1U) {
      throw FirewallError("iptables cannot express a mark list in one rule");
    }
    out += mark->kind == PhysicalMarkKind::conntrack ? " -m connmark"
                                                     : " -m mark";
    out += mark->negate ? " ! --mark " : " --mark ";
    append_hex(out, mark->values.front());
    out.push_back('/');
    append_hex(out, mark->mask);
  } else {
    throw FirewallError("cannot render an unknown iptables match");
  }
}

void append_iptables_target(std::string &out, const PhysicalStatement &stmt) {
  if (const auto *mark = std::get_if<SetMarkStmt>(&stmt)) {
    out += mark->kind == PhysicalMarkKind::conntrack
               ? " -j CONNMARK --set-xmark "
               : " -j MARK --set-xmark ";
    append_hex(out, mark->value);
    out.push_back('/');
    append_hex(out, mark->mask);
  } else if (const auto *copy = std::get_if<CopyMarkStmt>(&stmt)) {
    out += copy->to_conntrack ? " -j CONNMARK --save-mark"
                              : " -j CONNMARK --restore-mark";
    if (copy->nfmask == copy->ctmask) {
      out += " --mask ";
      append_hex(out, copy->nfmask);
    } else {
      out += " --nfmask ";
      append_hex(out, copy->nfmask);
      out += " --ctmask ";
      append_hex(out, copy->ctmask);
    }
  } else if (const auto *jump = std::get_if<JumpStmt>(&stmt)) {
    out += jump->is_goto ? " -g " : " -j ";
    out += jump->target.name;
  } else if (const auto *queue = std::get_if<QueueStmt>(&stmt)) {
    out += " -j NFQUEUE --queue-num ";
    out += std::to_string(queue->num);
    if (queue->bypass) out += " --queue-bypass";
  } else if (const auto *log = std::get_if<LogStmt>(&stmt)) {
    out += " -j NFLOG --nflog-group ";
    out += std::to_string(log->group);
    if (log->snaplen != 0) {
      out += " --nflog-size ";
      out += std::to_string(log->snaplen);
    }
    if (log->threshold != 1) {
      out += " --nflog-threshold ";
      out += std::to_string(log->threshold);
    }
  } else if (const auto *verdict = std::get_if<VerdictStmt>(&stmt)) {
    switch (verdict->verdict) {
    case PhysicalVerdict::accept:
      out += " -j ACCEPT";
      break;
    case PhysicalVerdict::drop:
      out += " -j DROP";
      break;
    case PhysicalVerdict::return_:
      out += " -j RETURN";
      break;
    }
  } else {
    throw FirewallError("cannot render an unsupported iptables statement");
  }
}

} // namespace

std::string render_iptables_rule(const PhysicalRule &rule,
                                 const std::string &chain) {
  if (rule.statements.size() != 1U) {
    throw FirewallError("an iptables rule has exactly one target");
  }
  std::string line;
  line.reserve(160);
  line += "-A ";
  line += chain;
  // Stable order by emission rank; rules have a handful of matches.
  bool after_conntrack = false;
  for (int rank = 0; rank <= 8; ++rank) {
    for (const auto &match : rule.matches) {
      if (iptables_match_rank(match) == rank) {
        append_iptables_match(line, match, after_conntrack);
        after_conntrack = is_conntrack_match(match);
      }
    }
  }
  if (rule.key.has_value()) {
    line += " -m comment --comment ";
    line += rule.key->comment();
  }
  append_iptables_target(line, rule.statements.front());
  line.push_back('\n');
  return line;
}

std::string IptablesFirewall::build_table_script(
    const char *table, FirewallFamily family,
    const std::vector<OwnedChainSpec> &chains, const PhysicalRuleset &rules,
    const std::string &observed_dump) {
  const PhysicalTable physical_table =
      std::string_view(table) == "raw" ? PhysicalTable::raw
                                       : PhysicalTable::mangle;
  const ObservedTable observed = parse_observed_table(observed_dump);

  // Chains this commit flushes: the declared owned chains plus every retired
  // A/B chain that exists.
  std::set<std::string> flushed;
  for (const auto &chain : chains) {
    flushed.insert(chain.name);
  }
  std::vector<std::string> legacy;
  for (const char *name : kLegacyChains) {
    if (observed.chains.count(name) != 0 && flushed.count(name) == 0) {
      legacy.emplace_back(name);
      flushed.insert(name);
    }
  }

  // A retired chain is deleted only when nothing outside this commit still
  // reaches it: its referrers are flushed chains or plain builtin hooks that
  // the same commit removes.  Anything else is left alone (and reported by
  // the verifier as drift).
  std::vector<std::string> deletable;
  std::vector<std::pair<std::string, std::string>> legacy_hooks;
  for (const auto &name : legacy) {
    bool free = true;
    std::vector<std::pair<std::string, std::string>> hooks;
    for (const auto &jump : observed.jumps) {
      if (jump.target != name || flushed.count(jump.source) != 0) {
        continue;
      }
      if (observed.builtins.count(jump.source) != 0 && jump.exact) {
        hooks.emplace_back(jump.source, name);
      } else {
        free = false;
        break;
      }
    }
    if (free) {
      deletable.push_back(name);
      legacy_hooks.insert(legacy_hooks.end(), hooks.begin(), hooks.end());
    }
  }

  std::string s;
  s += '*';
  s += table;
  s += '\n';
  for (const auto &chain : chains) {
    s += ':' + chain.name + " - [0:0]\n";
  }
  for (const auto &name : legacy) {
    s += ':' + name + " - [0:0]\n";
  }
  for (const auto &[source, target] : legacy_hooks) {
    s += "-D " + source + " -j " + target + '\n';
  }
  // A pinned chain is hooked only from the builtin chains it asks for: drop
  // the jumps of a hook that is no longer wanted (the OUTPUT hook of the sniff
  // chain once router traffic is not processed).
  for (const auto &chain : chains) {
    if (!chain.pinned) continue;
    for (const auto &jump : observed.jumps) {
      if (jump.target == chain.name && jump.exact &&
          observed.builtins.count(jump.source) != 0 &&
          jump.source != chain.hook_chain &&
          (chain.extra_hook_chain == nullptr ||
           jump.source != chain.extra_hook_chain)) {
        s += "-D " + jump.source + " -j " + jump.target + '\n';
      }
    }
  }
  for (const auto &chain : chains) {
    const PhysicalChain *physical =
        rules.find(iptables_physical_chain_id(chain.name, physical_table,
                                              family));
    if (physical == nullptr) {
      continue;
    }
    s.reserve(s.size() + physical->rules.size() * 160U);
    for (const auto &rule : physical->rules) {
      s += render_iptables_rule(rule, chain.name);
    }
  }
  // Exactly one hook per chain: add a missing one, drop duplicates.  The
  // builtin chains are never declared, so foreign rules stay untouched.
  // Pinned hooks (interception chains) must also be the first rule of their
  // builtin chain: a misplaced or duplicated jump is deleted and re-inserted
  // at position 1.
  for (const auto &chain : chains) {
    for (const char *hook_chain : {chain.hook_chain, chain.extra_hook_chain}) {
      if (hook_chain == nullptr) continue;
      std::size_t count = 0;
      std::size_t first_index = 0;
      for (const auto &jump : observed.jumps) {
        if (jump.exact && jump.source == hook_chain &&
            jump.target == chain.name) {
          if (count == 0 || jump.index < first_index) first_index = jump.index;
          ++count;
        }
      }
      if (chain.pinned) {
        if (count == 1 && first_index == 0) continue;
        for (; count > 0; --count) {
          s += std::string("-D ") + hook_chain + " -j " + chain.name + '\n';
        }
        s += std::string("-I ") + hook_chain + " 1 -j " + chain.name + '\n';
        continue;
      }
      if (count == 0) {
        s += std::string("-A ") + hook_chain + " -j " + chain.name + '\n';
      }
      for (; count > 1; --count) {
        s += std::string("-D ") + hook_chain + " -j " + chain.name + '\n';
      }
    }
  }
  for (const auto &name : deletable) {
    s += "-X " + name + '\n';
  }
  s += "COMMIT\n";
  return s;
}

FirewallLoweringContext
IptablesFirewall::lowering_context(uint32_t fwmark_mask) const {
  FirewallLoweringContext context;
  context.backend = FirewallBackend::iptables;
  context.raw_prerouting = raw_prerouting_;
  context.ipv6_enabled = ipv6_enabled();
  context.comments_ipv4_supported = comment_v4_supported_;
  context.comments_ipv6_supported = comment_v6_supported_;
  context.fwmark_mask = fwmark_mask;
  context.physical_set_name = [this](const std::string &name) {
    return physical_set_name(name);
  };
  return context;
}

PhysicalRuleset IptablesFirewall::expected_hook_rules() const {
  PhysicalRuleset result;
  const auto add_hook = [&result](const char *builtin, PhysicalTable table,
                                  FirewallFamily family,
                                  const PhysicalChainId &target) {
    PhysicalChain chain;
    chain.id = iptables_physical_chain_id(builtin, table, family);
    PhysicalRule rule;
    rule.family = family;
    rule.statements.push_back(JumpStmt{target, false});
    chain.rules.push_back(std::move(rule));
    result.chains.push_back(std::move(chain));
  };
  const auto add_family = [&](bool ipv6) {
    const auto family = ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
    const PhysicalTable pre_table =
        uses_raw_prerouting(ipv6) ? PhysicalTable::raw : PhysicalTable::mangle;
    add_hook("PREROUTING", pre_table, family,
             iptables_physical_chain_id(prerouting_chain_name(ipv6), pre_table,
                                        family));
    add_hook("OUTPUT", PhysicalTable::mangle, family,
             iptables_physical_chain_id(iptables_output_chain_name(),
                                        PhysicalTable::mangle, family));
  };
  add_family(false);
  if (ipv6_enabled() && ipv6_backend_available()) {
    add_family(true);
  }
  return result;
}

namespace {

// Jumps into the interception chains of the lowered ruleset, pinned to the
// first rule of their builtin chain (so they precede the classification hooks
// in the expected order).
void add_intercept_hook_rules(PhysicalRuleset &result) {
  struct Hook {
    const char *builtin;
    FirewallFamily family;
    PhysicalChainId target;
  };
  std::vector<Hook> hooks;
  for (const auto &chain : result.chains) {
    if (chain.id.role == PhysicalChainRole::iptables_dns_hold) {
      hooks.push_back({"POSTROUTING", chain.id.family, chain.id});
    } else if (chain.id.role == PhysicalChainRole::iptables_sniff) {
      hooks.push_back({"FORWARD", chain.id.family, chain.id});
      if (chain.output_hook) {
        hooks.push_back({"OUTPUT", chain.id.family, chain.id});
      }
    } else if (chain.id.role == PhysicalChainRole::iptables_sniff_out) {
      hooks.push_back({"OUTPUT", chain.id.family, chain.id});
    }
  }
  for (const auto &hook : hooks) {
    const PhysicalChainId id = iptables_physical_chain_id(
        hook.builtin, PhysicalTable::mangle, hook.family);
    PhysicalChain *chain = nullptr;
    for (auto &candidate : result.chains) {
      if (candidate.id == id) {
        chain = &candidate;
        break;
      }
    }
    if (chain == nullptr) {
      result.chains.push_back(PhysicalChain{id, std::nullopt, {}});
      chain = &result.chains.back();
    }
    PhysicalRule rule;
    rule.family = hook.family;
    rule.statements.push_back(JumpStmt{hook.target, false});
    rule.hook_position = 0;
    chain->rules.insert(chain->rules.begin(), std::move(rule));
  }
}

} // namespace

PhysicalRuleset
IptablesFirewall::expected_ruleset(const FirewallPlan &plan) const {
  auto context = lowering_context(plan.fwmark_mask);
  // apply_prepared() skips the IPv6 tables when the IPv6 backend is missing.
  context.ipv6_enabled = ipv6_enabled() && ipv6_backend_available();
  PhysicalRuleset result = lower_firewall_plan(plan, context);
  append_physical_ruleset(result, expected_hook_rules());
  add_intercept_hook_rules(result);
  return result;
}

void IptablesFirewall::clear_pending() {
  pending_sets_.clear();
  pending_elements_.clear();
  pending_ruleset_ = {};
  apply_prepared_ = false;
}

void IptablesFirewall::compile_plan(const FirewallPlan& plan,
                                    FirewallApplyMode mode) {
  (void)mode;
  set_fwmark_mask(plan.fwmark_mask);
  for (const auto& declaration : plan.sets) {
    const int family = declaration.family == FirewallFamily::ipv6 ? AF_INET6
                                                                   : AF_INET;
    create_ipset(physical_set_name(declaration.name), family,
                 declaration.timeout);
  }
  pending_ruleset_ =
      lower_firewall_plan(plan, lowering_context(plan.fwmark_mask));
}

void IptablesFirewall::apply(const FirewallPlan& plan, FirewallApplyMode mode) {
  try {
    validate_firewall_plan_backend(plan, backend());
    if (!apply_prepared_) {
      throw FirewallError("iptables apply was not prepared");
    }
    if (prepared_mode_ != mode) {
      throw FirewallError("iptables apply mode differs from prepare_apply");
    }
    compile_plan(plan, mode);
    apply_prepared(mode);
  } catch (...) {
    clear_pending();
    throw;
  }
}

void IptablesFirewall::apply_prepared(FirewallApplyMode mode) {
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
  // and daemon-owned set must already exist with the exact expected schema.
  // Do this before any rule transaction so a failure can safely fall back to
  // PreserveSets.
  if (mode == FirewallApplyMode::RulesOnly) {
    preflight_reused_set_schemas(effective_ipv6);
    if (!pending_elements_.empty()) {
      throw FirewallRulesOnlyError(
          "RulesOnly received buffered set elements; refusing to modify sets");
    }
  } else if (mode != FirewallApplyMode::Destructive) {
    preflight_dynamic_set_schemas(effective_ipv6);
  }

  if (mode == FirewallApplyMode::Destructive) {
    bool preserve_dynamic_sets = !clear_dynamic_sets_on_apply();
    if (preserve_dynamic_sets) {
      const auto incompatible =
          find_incompatible_dynamic_set_schema(effective_ipv6);
      if (incompatible.has_value()) {
        Logger::instance().warn(
            "incompatible managed dynamic ipset schema for {}; recreating "
            "dynamic ipsets and clearing learned entries",
            *incompatible);
        preserve_dynamic_sets = false;
      }
    }
    cleanup_live_impl(preserve_dynamic_sets,
                      /*sweep_live_state=*/true);
  }

  // Phase 1: create or refresh the sets.  A static set that does not exist is
  // created under its final name and filled.  One that exists is refreshed
  // through a temp set (create + fill, `swap`, destroy), so the rules keep
  // pointing at the final name and never see an empty set.  Any failure aborts
  // before the rules are touched.  Destructive already destroyed every static
  // set, so nothing exists there.
  std::set<std::string> live_names;
  if (mode != FirewallApplyMode::Destructive &&
      mode != FirewallApplyMode::RulesOnly) {
    auto names = list_live_ipset_names();
    if (!names.has_value()) {
      throw FirewallError("failed to inspect live ipsets");
    }
    live_names = std::move(*names);
  }
  std::set<std::string> kept_static_sets;
  if (mode != FirewallApplyMode::RulesOnly) {
    struct Refresh {
      std::string final_name;
      std::string temp_name;
    };
    std::vector<Refresh> refreshes;
    std::map<std::string, std::string> temp_of;
    std::string ipset_script;
    std::set<std::string> disabled_ipv6_sets;
    for (const auto &ps : pending_sets_) {
      if (ps.family_str == "inet6" && !effective_ipv6) {
        disabled_ipv6_sets.insert(ps.name);
        // Not managed this time, but not retired either.
        kept_static_sets.insert(ps.name);
        continue;
      }
      if (is_dynamic_set_name(ps.name)) {
        // The daemon owns these entries. A routine re-apply must neither
        // flush them nor alter their existing contents. Re-declaring with
        // -exist also recreates a set lost during an external firewall flush.
        ipset_script += build_ipset_create_line(ps);
        if (mode == FirewallApplyMode::Destructive &&
            clear_dynamic_sets_on_apply()) {
          ipset_script += keen_pbr3::format("flush {}\n", ps.name);
        }
        continue;
      }
      kept_static_sets.insert(ps.name);
      PendingSet target = ps;
      if (live_names.count(ps.name) != 0) {
        if (auto temp = temp_set_name(ps.name)) {
          // A stale temp from an interrupted refresh is dropped first.
          if (live_names.count(*temp) != 0 && destroy_set_best_effort(*temp)) {
            live_names.erase(*temp);
          }
          target.name = *temp;
          temp_of.emplace(ps.name, *temp);
          refreshes.push_back({ps.name, std::move(*temp)});
        }
      }
      ipset_script += build_ipset_create_line(target);
      ipset_script += keen_pbr3::format("flush {}\n", target.name);
    }
    for (auto &[set_name, buf] : pending_elements_) {
      if (disabled_ipv6_sets.find(set_name) != disabled_ipv6_sets.end()) {
        continue;
      }
      const std::string elements = buf.str();
      if (elements.empty()) {
        continue;
      }
      const auto temp = temp_of.find(set_name);
      if (temp == temp_of.end()) {
        ipset_script += elements;
      } else {
        append_retargeted_elements(ipset_script, elements, set_name,
                                   temp->second);
      }
    }
    const auto drop_temps = [&refreshes] {
      for (const auto &refresh : refreshes) {
        destroy_set_best_effort(refresh.temp_name);
      }
    };
    try {
      if (!ipset_script.empty()) {
        pipe_to_cmd({"ipset", "restore", "-exist"}, ipset_script);
      }
      for (const auto &refresh : refreshes) {
        const int status = safe_exec(
            {"ipset", "swap", refresh.temp_name, refresh.final_name},
            /*suppress_output=*/false);
        if (status != 0) {
          throw FirewallError(keen_pbr3::format(
              "ipset swap {} {} exited with status {}", refresh.temp_name,
              refresh.final_name, status));
        }
      }
    } catch (...) {
      drop_temps();
      throw;
    }
    // The temp sets now hold the previous contents.
    drop_temps();
  }

  // Phase 2: one iptables-restore transaction per table and family.  Each
  // declares (flushes) our chains, appends the lowered rules, ensures the
  // single builtin hook and retires legacy A/B chains in the same commit.
  const auto restore_family = [this](bool ipv6) {
    const char *command = ipv6 ? "ip6tables-restore" : "iptables-restore";
    const auto family = ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
    const OwnedChainSpec output{iptables_output_chain_name(), "OUTPUT"};
    // Interception chains (always mangle), only when the plan carries them.
    std::vector<OwnedChainSpec> intercept;
    const auto planned = [&](const char *name) {
      return pending_ruleset_.find(iptables_physical_chain_id(
                 name, PhysicalTable::mangle, family)) != nullptr;
    };
    if (planned(iptables_dns_hold_chain_name())) {
      intercept.push_back(
          {iptables_dns_hold_chain_name(), "POSTROUTING", true, nullptr});
    }
    if (planned(iptables_sniff_chain_name())) {
      const bool output_hook =
          pending_ruleset_
              .find(iptables_physical_chain_id(iptables_sniff_chain_name(),
                                               PhysicalTable::mangle, family))
              ->output_hook;
      intercept.push_back({iptables_sniff_chain_name(), "FORWARD", true,
                           output_hook ? "OUTPUT" : nullptr});
    }
    if (planned(iptables_sniff_out_chain_name())) {
      intercept.push_back(
          {iptables_sniff_out_chain_name(), "OUTPUT", true, nullptr});
    }
    const auto run = [&](const char *table,
                         const std::vector<OwnedChainSpec> &chains) {
      pipe_to_cmd({command, "--noflush", "--counters"},
                  build_table_script(table, family, chains, pending_ruleset_,
                                     capture_table_dump(ipv6, table)));
    };
    if (uses_raw_prerouting(ipv6)) {
      // Publish local OUTPUT first and the forwarded-traffic PREROUTING path
      // last.
      std::vector<OwnedChainSpec> mangle{output};
      mangle.insert(mangle.end(), intercept.begin(), intercept.end());
      run("mangle", mangle);
      run("raw", {{prerouting_chain_name(ipv6), "PREROUTING"}});
    } else {
      std::vector<OwnedChainSpec> mangle{
          {prerouting_chain_name(ipv6), "PREROUTING"}, output};
      mangle.insert(mangle.end(), intercept.begin(), intercept.end());
      run("mangle", mangle);
    }
  };

  restore_family(false);
  chain_v4_created_ = true;
  if (effective_ipv6) {
    restore_family(true);
    chain_v6_created_ = true;
  }

  // Every enabled table has converged; confirm exactly one hook each.
  verify_applied_hooks(false);
  if (effective_ipv6) {
    verify_applied_hooks(true);
  }

  // Phase 3: the new rules are live; destroy the owned static sets they no
  // longer reference (removed lists, retired A/B generation sets).  An "in
  // use" set is reported and left for the next apply.
  if (mode != FirewallApplyMode::Destructive &&
      mode != FirewallApplyMode::RulesOnly) {
    destroy_unreferenced_static_sets(live_names, kept_static_sets);
  }

  // Clear pending buffers
  pending_sets_.clear();
  pending_elements_.clear();
  pending_ruleset_ = {};
}

void IptablesFirewall::cleanup_rules_impl(bool sweep_live_state) {
  const auto flush_delete = [](const char *command, const char *table,
                               const std::string &chain) {
    run_cleanup_command({command, "-t", table, "-F", chain});
    run_cleanup_command({command, "-t", table, "-X", chain});
  };
  const auto cleanup_family = [&](bool ipv6, bool owned) {
    if (!owned && !sweep_live_state) {
      return;
    }
    const char *command = ipv6 ? "ip6tables" : "iptables";
    const bool raw = uses_raw_prerouting(ipv6);

    // A live-state sweep deliberately handles the current and the retired
    // layouts.  This is limited to named keen-pbr chains and hooks; no table
    // is flushed.  Hooks go first so no chain is referenced when deleted, and
    // retired dispatchers go before the generation chains they reference.
    if (sweep_live_state || raw) {
      remove_all_hooks(command, "raw", "PREROUTING", "KeenPbrRaw");
      for (const char *chain :
           {"KeenPbrRaw", "KeenPbrRaw_A", "KeenPbrRaw_B"}) {
        flush_delete(command, "raw", chain);
      }
    }
    remove_all_hooks(command, "mangle", "OUTPUT", "KeenPbrOutput");
    remove_all_hooks(command, "mangle", "OUTPUT", "KeenPbrTable_OUTPUT");
    // Interception chains: hooks first, then the chains.
    remove_all_hooks(command, "mangle", "POSTROUTING", "KeenPbrDnsHold");
    remove_all_hooks(command, "mangle", "FORWARD", "KeenPbrSniff");
    remove_all_hooks(command, "mangle", "OUTPUT", "KeenPbrSniff");
    remove_all_hooks(command, "mangle", "OUTPUT", "KeenPbrSniffOut");
    if (sweep_live_state || !raw) {
      remove_all_hooks(command, "mangle", "PREROUTING", "KeenPbrTable");
    }
    for (const char *chain :
         {"KeenPbrDnsHold", "KeenPbrSniff", "KeenPbrSniffOut"}) {
      flush_delete(command, "mangle", chain);
    }
    for (const char *chain : {"KeenPbrOutput", "KeenPbrTable_OUTPUT"}) {
      flush_delete(command, "mangle", chain);
    }
    if (sweep_live_state || !raw) {
      flush_delete(command, "mangle", "KeenPbrTable");
    }
    for (const char *chain : {"KeenPbrOutput_A", "KeenPbrOutput_B",
                              "KeenPbrTable_A", "KeenPbrTable_B"}) {
      flush_delete(command, "mangle", chain);
    }
  };

  cleanup_family(false, chain_v4_created_);
  cleanup_family(true, chain_v6_created_);
  chain_v4_created_ = false;
  chain_v6_created_ = false;

  if (sweep_live_state) {
    cleanup_legacy_numbered_chains("iptables");
    cleanup_legacy_numbered_chains("ip6tables");
  }
}

void IptablesFirewall::cleanup_legacy_numbered_chains(const char *command) {
  const auto result = run_cleanup_command({command, "-t", "mangle", "-S"});

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
    run_cleanup_command({command, "-t", "mangle", "-F", chain});
    run_cleanup_command({command, "-t", "mangle", "-X", chain});
  }
}

void IptablesFirewall::cleanup_saved_sets(bool preserve_dynamic_sets) {
  const auto result = run_cleanup_command({"ipset", "save"});

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
        name.rfind("kpbr4t_", 0) == 0 || name.rfind("kpbr6t_", 0) == 0 ||
        name.rfind("kpbr4s_", 0) == 0 || name.rfind("kpbr6s_", 0) == 0 ||
        name.rfind("kpbr4S_", 0) == 0 || name.rfind("kpbr6S_", 0) == 0;
    if (!managed_static && !dynamic) {
      continue;
    }
    if (dynamic && preserve_dynamic_sets) {
      continue;
    }
    run_cleanup_command({"ipset", "flush", name});
    run_cleanup_command({"ipset", "destroy", name});
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
    run_cleanup_command({"ipset", "flush", name});
    run_cleanup_command({"ipset", "destroy", name});
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
  pending_ruleset_ = {};
}

void IptablesFirewall::cleanup() { cleanup_impl(); }

FirewallBackend IptablesFirewall::backend() const {
  return FirewallBackend::iptables;
}

std::unique_ptr<Firewall>
create_iptables_firewall(RawPreroutingMode raw_prerouting) {
  return std::make_unique<IptablesFirewall>(raw_prerouting);
}

} // namespace keen_pbr3
