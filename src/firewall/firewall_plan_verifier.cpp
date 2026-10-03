#include "firewall_plan_verifier.hpp"

#include "../util/format_compat.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace keen_pbr3 {
namespace {

// The multiset pass on the part of a chain between the common prefix and
// common suffix is quadratic; past this product only positional equality is
// used (still correct, merely less specific about reordering).
constexpr std::size_t kMaxPairwiseMatches = 16384;
// Differences reported per chain before they are summarized.
constexpr std::size_t kMaxReportsPerChain = 16;

// ---------------------------------------------------------------------------
// Compact descriptions (diagnostics only)
// ---------------------------------------------------------------------------

std::string join_strings(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += ',';
        result += value;
    }
    return result;
}

std::string join_numbers(const std::vector<uint32_t>& values, bool hex) {
    std::string result;
    for (const auto value : values) {
        if (!result.empty()) result += ',';
        result += hex ? keen_pbr3::format("{:#x}", value)
                      : std::to_string(value);
    }
    return result;
}

const char* dir_name(PhysicalDir dir) {
    return dir == PhysicalDir::src ? "src" : "dst";
}

const char* neg(bool negate) { return negate ? "!" : ""; }

const char* mark_kind_name(PhysicalMarkKind kind) {
    return kind == PhysicalMarkKind::conntrack ? "ctmark" : "mark";
}

std::string chain_label(const PhysicalChainId& id) {
    if (id.table == PhysicalTable::nft_inet) return id.name;
    std::string label = id.table == PhysicalTable::raw ? "raw/" : "mangle/";
    label += id.name;
    if (id.family == FirewallFamily::ipv6) label += " (ipv6)";
    return label;
}

struct MatchDescriber {
    std::string operator()(const SetMatch& m) const {
        return keen_pbr3::format("set{}{}:{}", neg(m.negate), dir_name(m.dir),
                                 m.name);
    }
    std::string operator()(const AddrMatch& m) const {
        return keen_pbr3::format("{}{}={}", dir_name(m.dir), neg(m.negate),
                                 join_strings(m.cidrs));
    }
    std::string operator()(const ProtoMatch& m) const {
        return std::string("proto=") + l4_proto_name(m.proto);
    }
    std::string operator()(const PortMatch& m) const {
        std::string ranges;
        for (const auto& range : m.ranges) {
            if (!ranges.empty()) ranges += ',';
            ranges += range.from == range.to
                ? std::to_string(range.from)
                : std::to_string(range.from) + "-" + std::to_string(range.to);
        }
        return keen_pbr3::format("{}port{}={}", dir_name(m.dir), neg(m.negate),
                                 ranges);
    }
    std::string operator()(const DscpMatch& m) const {
        return "dscp=" + std::to_string(static_cast<unsigned>(m.value));
    }
    std::string operator()(const IifMatch& m) const {
        return keen_pbr3::format("iif{}={}", neg(m.negate),
                                 join_strings(m.names));
    }
    std::string operator()(const MarkMatch& m) const {
        return keen_pbr3::format("{}&{:#x}{}={}", mark_kind_name(m.kind),
                                 m.mask, m.negate ? "!" : "",
                                 join_numbers(m.values, true));
    }
    std::string operator()(const CtStateMatch& m) const {
        return keen_pbr3::format("ct-state{}={:#x}", neg(m.negate),
                                 static_cast<unsigned>(m.states));
    }
    std::string operator()(const CtDirMatch& m) const {
        return m.original ? "ct-dir=original" : "ct-dir=reply";
    }
    std::string operator()(const ConnbytesMatch& m) const {
        const char* dir = m.dir == ConnbytesDir::original
            ? "original" : (m.dir == ConnbytesDir::reply ? "reply" : "both");
        const char* mode = m.mode == ConnbytesMode::packets ? "packets" : "bytes";
        return keen_pbr3::format("connbytes {} {}-{} ({})", dir, m.from, m.to,
                                 mode);
    }
    std::string operator()(const UnknownMatch& m) const {
        return "unknown(" + m.text + ")";
    }
};

std::string describe_statement(const PhysicalStatement& statement);

std::string describe_chain_ref(const PhysicalChainId& id) {
    return id.name.empty() ? "?" : id.name;
}

struct StatementDescriber {
    std::string operator()(const SetMarkStmt& s) const {
        return keen_pbr3::format("set-{} {:#x}/{:#x}", mark_kind_name(s.kind),
                                 s.value, s.mask);
    }
    std::string operator()(const CopyMarkStmt& s) const {
        return keen_pbr3::format("{} {:#x}/{:#x}",
                                 s.to_conntrack ? "save-mark" : "restore-mark",
                                 s.nfmask, s.ctmask);
    }
    std::string operator()(const JumpStmt& s) const {
        return std::string(s.is_goto ? "goto " : "jump ") +
               describe_chain_ref(s.target);
    }
    std::string operator()(const VerdictStmt& s) const {
        switch (s.verdict) {
        case PhysicalVerdict::accept: return "accept";
        case PhysicalVerdict::drop: return "drop";
        case PhysicalVerdict::return_: return "return";
        }
        return "?";
    }
    std::string operator()(const QueueStmt& s) const {
        return keen_pbr3::format("nfqueue {}{}", s.num,
                                 s.bypass ? " bypass" : "");
    }
    std::string operator()(const LogStmt& s) const {
        return keen_pbr3::format("nflog group {} snaplen {} threshold {}",
                                 s.group, s.snaplen, s.threshold);
    }
    std::string operator()(const VmapStmt& s) const {
        std::string entries;
        for (const auto& [key, target] : s.entries) {
            if (!entries.empty()) entries += ',';
            entries += std::to_string(key) + ":" + describe_chain_ref(target);
        }
        return keen_pbr3::format(
            "vmap {}{:#x} {{{}}}",
            s.key == PhysicalVmapKey::numgen_inc ? "numgen mod " : "ctmark&",
            s.param, entries);
    }
    std::string operator()(const UnknownStmt& s) const {
        return "unknown(" + s.text + ")";
    }
    std::string operator()(const LateMatchStmt& s) const {
        return "then " + std::visit(MatchDescriber{}, s.match);
    }
};

std::string describe_statement(const PhysicalStatement& statement) {
    return std::visit(StatementDescriber{}, statement);
}

std::string describe_rule(const PhysicalRule& rule) {
    std::string result;
    if (rule.family == FirewallFamily::ipv4) result = "ipv4 ";
    if (rule.family == FirewallFamily::ipv6) result = "ipv6 ";
    for (const auto& match : rule.matches) {
        result += std::visit(MatchDescriber{}, match);
        result += ' ';
    }
    result += "->";
    for (const auto& statement : rule.statements) {
        result += ' ';
        result += describe_statement(statement);
    }
    if (rule.hook_position.has_value()) {
        result += keen_pbr3::format(" (position {})", *rule.hook_position);
    }
    return result;
}

std::string describe_base(const std::optional<PhysicalBaseChain>& base) {
    if (!base.has_value()) return "none";
    const char* hook = "other";
    switch (base->hook) {
    case PhysicalBaseChain::Hook::prerouting: hook = "prerouting"; break;
    case PhysicalBaseChain::Hook::output: hook = "output"; break;
    case PhysicalBaseChain::Hook::forward: hook = "forward"; break;
    case PhysicalBaseChain::Hook::postrouting: hook = "postrouting"; break;
    case PhysicalBaseChain::Hook::other: break;
    }
    const char* type = base->type == PhysicalBaseChain::Type::filter
        ? "filter"
        : (base->type == PhysicalBaseChain::Type::route ? "route" : "other");
    return keen_pbr3::format("{} hook {} priority {} policy {}", type, hook,
                             base->priority,
                             base->policy_accept ? "accept" : "drop");
}

// The fwmark a rule installs: a direct packet mark, or the setter chain it
// jumps to.
std::optional<uint32_t> installed_mark(const PhysicalRule& rule) {
    for (const auto& statement : rule.statements) {
        if (const auto* set = std::get_if<SetMarkStmt>(&statement)) {
            if (set->kind == PhysicalMarkKind::packet) return set->value;
        } else if (const auto* jump = std::get_if<JumpStmt>(&statement)) {
            if (jump->target.role == PhysicalChainRole::nft_setter) {
                return jump->target.setter_mark;
            }
        }
    }
    return std::nullopt;
}

const char* chain_kind(PhysicalChainRole role) {
    switch (role) {
    case PhysicalChainRole::system_prerouting:
    case PhysicalChainRole::system_output:
    case PhysicalChainRole::system_other:
        return "hook";
    case PhysicalChainRole::nft_setter:
        return "setter";
    default:
        return "chain";
    }
}

bool is_system_role(PhysicalChainRole role) {
    return role == PhysicalChainRole::system_prerouting ||
           role == PhysicalChainRole::system_output ||
           role == PhysicalChainRole::system_other;
}

std::string action_name(const FirewallRuleAction& action) {
    if (std::holds_alternative<MarkAction>(action)) return "mark";
    if (std::holds_alternative<BalanceAction>(action)) return "balance";
    if (const auto* verdict = std::get_if<VerdictAction>(&action)) {
        return *verdict == VerdictAction::drop ? "drop" : "pass";
    }
    if (std::holds_alternative<RestoreConntrackMarkAction>(action)) {
        return "restore_conntrack_mark";
    }
    if (std::holds_alternative<SkipEstablishedOrDnatAction>(action)) {
        return "skip_established_or_dnat";
    }
    if (std::holds_alternative<SkipMarkedPacketsAction>(action)) {
        return "skip_marked_packets";
    }
    if (std::holds_alternative<QueueAction>(action)) return "queue";
    if (std::holds_alternative<LogAction>(action)) return "log";
    return "inbound_interface";
}

// ---------------------------------------------------------------------------
// Report assembly
// ---------------------------------------------------------------------------

class Report {
public:
    Report(const FirewallPlan& plan, const PhysicalRuleset& expected)
        : plan_(plan), checks_(plan.rules.size()),
          lowered_(plan.rules.size(), 0) {
        for (std::size_t i = 0; i < plan.rules.size(); ++i) {
            const auto& rule = plan.rules[i];
            auto& check = checks_[i];
            check.set_name = rule.criteria.dst_set_name.has_value()
                ? *rule.criteria.dst_set_name : "<direct>";
            check.action = action_name(rule.action);
            if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
                check.expected_fwmark = mark->value;
            } else if (const auto* balance =
                           std::get_if<BalanceAction>(&rule.action)) {
                check.expected_fwmark = balance->fallback_mark;
            }
            check.status = CheckStatus::ok;
        }
        for (const auto& chain : expected.chains) {
            for (const auto& rule : chain.rules) {
                if (rule.plan_rule < lowered_.size()) {
                    lowered_[rule.plan_rule] = 1;
                }
            }
        }
    }

    // Attribute a difference to the plan rule that produced `rule`; false when
    // the rule belongs to no plan rule.
    bool flag_plan(const PhysicalRule& rule, CheckStatus status,
                   const std::string& detail,
                   std::optional<uint32_t> actual_mark = std::nullopt) {
        if (rule.plan_rule >= checks_.size()) return false;
        auto& check = checks_[rule.plan_rule];
        if (check.status != CheckStatus::ok) return true;  // first problem wins
        const auto& key = plan_.rules[rule.plan_rule].key;
        check.status = status;
        check.detail = keen_pbr3::format("key={}:{} {}", key.module_id,
                                         key.instance_id, detail);
        check.actual_fwmark = actual_mark;
        return true;
    }

    void add_extra(const PhysicalChainId& chain, CheckStatus status,
                   std::string detail,
                   std::optional<uint32_t> expected_mark = std::nullopt,
                   std::optional<uint32_t> actual_mark = std::nullopt) {
        FirewallRuleCheck check;
        check.set_name = chain_label(chain);
        check.action = chain_kind(chain.role);
        check.expected_fwmark = expected_mark;
        check.actual_fwmark = actual_mark;
        check.status = status;
        check.detail = std::move(detail);
        extras_.push_back(std::move(check));
    }

    // Flag the plan rule when there is one, otherwise report on the chain.
    void report(const PhysicalChainId& chain, const PhysicalRule& rule,
                CheckStatus status, const std::string& detail,
                std::optional<uint32_t> actual_mark = std::nullopt) {
        if (!flag_plan(rule, status, detail, actual_mark)) {
            add_extra(chain, status, detail, std::nullopt, actual_mark);
        }
    }

    void fail_all(const std::string& detail) {
        for (std::size_t i = 0; i < checks_.size(); ++i) {
            if (lowered_[i] != 0) {
                checks_[i].status = CheckStatus::missing;
                checks_[i].detail = detail;
            }
        }
    }

    std::vector<FirewallRuleCheck> finish() {
        std::vector<FirewallRuleCheck> result;
        result.reserve(checks_.size() + extras_.size());
        for (std::size_t i = 0; i < checks_.size(); ++i) {
            if (lowered_[i] == 0) continue;
            auto& check = checks_[i];
            if (check.status == CheckStatus::ok) {
                check.detail = "ok";
                if (std::holds_alternative<MarkAction>(plan_.rules[i].action)) {
                    check.actual_fwmark = check.expected_fwmark;
                }
            }
            result.push_back(std::move(check));
        }
        for (auto& extra : extras_) result.push_back(std::move(extra));
        return result;
    }

private:
    const FirewallPlan& plan_;
    std::vector<FirewallRuleCheck> checks_;
    std::vector<char> lowered_;
    std::vector<FirewallRuleCheck> extras_;
};

// ---------------------------------------------------------------------------
// Ordered diff of one chain
// ---------------------------------------------------------------------------

void diff_rules(Report& report, const PhysicalChain& expected,
                const PhysicalChain& observed) {
    const auto& e = expected.rules;
    const auto& o = observed.rules;
    const std::size_t n = e.size();
    const std::size_t m = o.size();
    const std::string label = chain_label(expected.id);

    // Common prefix and suffix: one inserted, removed or edited rule leaves a
    // tiny middle however long the chain is.
    const std::size_t common = std::min(n, m);
    std::size_t prefix = 0;
    while (prefix < common && e[prefix] == o[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < common - prefix && e[n - 1 - suffix] == o[m - 1 - suffix]) {
        ++suffix;
    }
    const std::size_t e_end = n - suffix;
    const std::size_t o_end = m - suffix;
    if (prefix == e_end && prefix == o_end) return;

    const std::size_t e_count = e_end - prefix;
    const std::size_t o_count = o_end - prefix;

    // Multiset match of the middle: which expected rule is present anywhere.
    std::vector<std::ptrdiff_t> matched(e_count, -1);
    std::vector<char> observed_used(o_count, 0);
    if (e_count * o_count <= kMaxPairwiseMatches) {
        for (std::size_t j = 0; j < e_count; ++j) {
            for (std::size_t k = 0; k < o_count; ++k) {
                if (observed_used[k] == 0 && e[prefix + j] == o[prefix + k]) {
                    matched[j] = static_cast<std::ptrdiff_t>(prefix + k);
                    observed_used[k] = 1;
                    break;
                }
            }
        }
    } else {
        for (std::size_t j = 0; j < std::min(e_count, o_count); ++j) {
            if (e[prefix + j] == o[prefix + j]) {
                matched[j] = static_cast<std::ptrdiff_t>(prefix + j);
                observed_used[j] = 1;
            }
        }
    }

    std::size_t reported = 0;
    std::size_t suppressed = 0;
    // Differences attributed to a plan rule need no cap (bounded by the plan);
    // the rest is reported on the chain, capped.
    const auto emit = [&](const PhysicalRule& rule, CheckStatus status,
                          const std::string& detail,
                          std::optional<uint32_t> actual = std::nullopt) {
        if (report.flag_plan(rule, status, detail, actual)) return;
        if (reported++ < kMaxReportsPerChain) {
            report.add_extra(expected.id, status, detail, std::nullopt, actual);
        } else {
            ++suppressed;
        }
    };

    // Present but in a different order.
    std::ptrdiff_t highest = -1;
    for (std::size_t j = 0; j < e_count; ++j) {
        if (matched[j] < 0) continue;
        if (matched[j] < highest) {
            emit(e[prefix + j], CheckStatus::mismatch,
                 keen_pbr3::format(
                     "rule order differs in {}: expected at index {}, "
                     "observed at index {}: {}",
                     label, prefix + j, matched[j], describe_rule(e[prefix + j])));
        } else {
            highest = matched[j];
        }
    }

    std::vector<std::size_t> missing;
    std::vector<std::size_t> surplus;
    for (std::size_t j = 0; j < e_count; ++j) {
        if (matched[j] < 0) missing.push_back(prefix + j);
    }
    for (std::size_t k = 0; k < o_count; ++k) {
        if (observed_used[k] == 0) surplus.push_back(prefix + k);
    }

    // An unmatched expected rule facing an unmatched observed one is an edited
    // rule; the rest is missing or extra.
    const std::size_t paired = std::min(missing.size(), surplus.size());
    for (std::size_t i = 0; i < paired; ++i) {
        const auto& want = e[missing[i]];
        const auto& have = o[surplus[i]];
        std::string detail = keen_pbr3::format(
            "rule mismatch in {} at index {}: expected {} but observed {}",
            label, surplus[i], describe_rule(want), describe_rule(have));
        if (have.key.has_value()) {
            detail += keen_pbr3::format(" (observed key={}:{})",
                                        have.key->module_id,
                                        have.key->instance_id);
        }
        emit(want, CheckStatus::mismatch, detail, installed_mark(have));
    }
    for (std::size_t i = paired; i < missing.size(); ++i) {
        emit(e[missing[i]], CheckStatus::missing,
             keen_pbr3::format("rule missing in {} at index {}: {}", label,
                               missing[i], describe_rule(e[missing[i]])));
    }
    for (std::size_t i = paired; i < surplus.size(); ++i) {
        const auto& have = o[surplus[i]];
        const auto twin = std::find(e.begin(), e.end(), have);
        if (twin != e.end()) {
            emit(*twin, CheckStatus::mismatch,
                 keen_pbr3::format(
                     "duplicate rule in {} at index {} (expected once at "
                     "index {}): {}",
                     label, surplus[i], std::distance(e.begin(), twin),
                     describe_rule(have)));
        } else {
            std::string detail = keen_pbr3::format(
                "unexpected rule in {} at index {}: {}", label, surplus[i],
                describe_rule(have));
            if (have.key.has_value()) {
                detail += keen_pbr3::format(" (key={}:{})", have.key->module_id,
                                            have.key->instance_id);
            }
            if (reported++ < kMaxReportsPerChain) {
                report.add_extra(expected.id, CheckStatus::mismatch, detail);
            } else {
                ++suppressed;
            }
        }
    }
    if (suppressed != 0) {
        report.add_extra(expected.id, CheckStatus::mismatch,
                         keen_pbr3::format("{} more rule differences in {}",
                                           suppressed, label));
    }
}

void diff_chain(Report& report, const PhysicalChain& expected,
                const PhysicalChain* observed) {
    // A builtin chain is only observed while it holds a keen-pbr jump: absent
    // simply means no hook, i.e. every expected jump is missing.
    if (observed == nullptr && is_system_role(expected.id.role)) {
        PhysicalChain empty;
        empty.id = expected.id;
        diff_rules(report, expected, empty);
        return;
    }
    if (observed == nullptr) {
        const std::string detail =
            "chain " + chain_label(expected.id) + " is missing";
        std::optional<uint32_t> mark;
        if (expected.id.role == PhysicalChainRole::nft_setter) {
            mark = expected.id.setter_mark;
        }
        report.add_extra(expected.id, CheckStatus::missing, detail, mark);
        for (const auto& rule : expected.rules) {
            report.flag_plan(rule, CheckStatus::missing, detail);
        }
        return;
    }
    if (expected.base != observed->base) {
        report.add_extra(
            expected.id, CheckStatus::mismatch,
            keen_pbr3::format("base chain attributes of {} differ: expected {} "
                              "but observed {}",
                              chain_label(expected.id),
                              describe_base(expected.base),
                              describe_base(observed->base)));
    }
    diff_rules(report, expected, *observed);
}

} // namespace

bool firewall_expected_uses_ipv6(const PhysicalRuleset& expected) {
    return std::any_of(expected.chains.begin(), expected.chains.end(),
                       [](const PhysicalChain& chain) {
                           return chain.id.table != PhysicalTable::nft_inet &&
                                  chain.id.family == FirewallFamily::ipv6;
                       });
}

std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const PhysicalRuleset& expected,
    const FirewallSnapshot& snapshot) {
    Report report(plan, expected);
    if (!snapshot.available || !snapshot.error.empty()) {
        report.fail_all(snapshot.error.empty() ? "firewall snapshot unavailable"
                                               : snapshot.error);
        return report.finish();
    }

    for (const auto& chain : expected.chains) {
        diff_chain(report, chain, snapshot.ruleset.find(chain.id));
    }

    for (const auto& chain : snapshot.ruleset.chains) {
        if (expected.find(chain.id) != nullptr) {
            continue;
        }
        const std::string label = chain_label(chain.id);
        if (is_system_role(chain.id.role)) {
            // Only jumps into keen-pbr chains are ever observed here.
            for (std::size_t i = 0; i < chain.rules.size(); ++i) {
                report.add_extra(
                    chain.id, CheckStatus::mismatch,
                    keen_pbr3::format("unexpected rule in {} at index {}: {}",
                                      label, i, describe_rule(chain.rules[i])));
            }
        } else {
            report.add_extra(
                chain.id, CheckStatus::mismatch,
                keen_pbr3::format("unexpected keen-pbr chain {} ({} rules)",
                                  label, chain.rules.size()),
                std::nullopt,
                chain.id.role == PhysicalChainRole::nft_setter
                    ? std::optional<uint32_t>{chain.id.setter_mark}
                    : std::nullopt);
        }
    }
    return report.finish();
}

} // namespace keen_pbr3
