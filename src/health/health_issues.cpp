#include "health_issues.hpp"

#include "../util/format_compat.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxDetailEntries = 3;

template <typename Enum>
std::string enum_name(Enum value) {
    return nlohmann::json(value).get<std::string>();
}

bool healthy_status(const std::string& status) {
    return status == "ok" || status == "skipped";
}

std::string summarize_entries(const std::vector<std::string>& entries) {
    std::string out;
    for (std::size_t i = 0; i < entries.size() && i < kMaxDetailEntries; ++i) {
        if (!out.empty()) out += "; ";
        out += entries[i];
    }
    if (entries.size() > kMaxDetailEntries) {
        out += keen_pbr3::format("; +{}", entries.size() - kMaxDetailEntries);
    }
    return out;
}

std::string humanize_feature(std::string feature) {
    std::replace(feature.begin(), feature.end(), '_', ' ');
    if (!feature.empty() && feature[0] >= 'a' && feature[0] <= 'z') {
        feature[0] = static_cast<char>(feature[0] - 'a' + 'A');
    }
    return feature;
}

std::string capability_title(const std::string& feature) {
    if (feature == "nfqueue") return "NFQUEUE";
    if (feature == "nflog") return "NFLOG";
    return feature;
}

std::string hex32(uint32_t value) {
    return keen_pbr3::format("0x{:08x}", value);
}

std::string check_status_name(CheckStatus status) {
    switch (status) {
    case CheckStatus::ok: return "ok";
    case CheckStatus::missing: return "missing";
    case CheckStatus::mismatch: return "mismatch";
    }
    return "unknown";
}

std::string describe_route_mismatch(const RouteTableCheck& table) {
    std::vector<std::string> problems;
    if (!table.table_exists) problems.emplace_back("table missing");
    if (!table.default_route_present) problems.emplace_back("default route missing");
    if (!table.interface_matches) problems.emplace_back("interface mismatch");
    if (!table.gateway_matches) problems.emplace_back("gateway mismatch");
    if (problems.empty()) {
        return table.detail.empty() ? check_status_name(table.status) : table.detail;
    }
    std::string out;
    for (const auto& problem : problems) {
        if (!out.empty()) out += ", ";
        out += problem;
    }
    return out;
}

std::string host_warning_title(const HealthWarning& warning, const std::string& interface) {
    switch (warning.code) {
    case HealthWarningCode::nat_missing:
        return interface + ": no NAT (MASQUERADE/SNAT) for this WAN";
    case HealthWarningCode::nat_partial:
        return interface + ": NAT covers only some source subnets";
    case HealthWarningCode::rp_filter_strict:
        return interface + ": strict rp_filter may drop replies";
    case HealthWarningCode::fwmark_mask_conflict:
        return "A foreign rule uses packet mark bits reserved for keen-pbr";
    }
    return warning.message;
}

api::HealthIssue make_issue(std::string key, api::Severity severity, api::Code code,
                            std::string title) {
    api::HealthIssue issue;
    issue.key = std::move(key);
    issue.severity = severity;
    issue.code = code;
    issue.title = std::move(title);
    return issue;
}

void set_list(api::HealthIssue& issue, std::vector<std::string> entries) {
    issue.detail = summarize_entries(entries);
    issue.entries = std::move(entries);
}

std::string describe_member(const api::RuntimeInterfaceState& member) {
    std::string out = member.outbound_tag;
    out += " · " + enum_name(member.status);
    if (member.detail && !member.detail->empty()) out += " · " + *member.detail;
    return out;
}

void collect_kernel_issues(const std::vector<api::KernelCheckElement>& checks,
                           std::vector<api::HealthIssue>& issues) {
    for (const auto& check : checks) {
        if (healthy_status(check.status)) continue;
        const bool failed = check.status == "error" || check.status == "unsupported";
        const auto severity = failed && !check.advisory ? api::Severity::ERROR
                                                        : api::Severity::WARNING;
        if (check.kind == "extension") {
            const std::string kind = check.extension_kind.value_or("match");
            const std::string module = check.kernel_module.value_or("");
            auto issue = make_issue(
                check.key, severity, api::Code::XT_EXTENSION,
                keen_pbr3::format("iptables {} {} ({})", kind, check.feature, module));
            issue.params = {{"kind", kind}, {"name", check.feature}, {"module", module}};
            if (!check.advisory) {
                issue.detail_code = api::DetailCode::XT_EXTENSION_REQUIRED;
                issue.detail = "Kernel module is missing and the current rules need it: "
                               "the firewall rules cannot be applied";
            } else if (check.feature == "addrtype") {
                // Without addrtype the skip rules fall back to plain address matches.
                issue.detail_code = api::DetailCode::CAPABILITY_FALLBACK;
                issue.detail = "Not available in the kernel; plain address rules are used instead";
            } else {
                issue.detail_code = api::DetailCode::XT_EXTENSION_UNUSED;
                issue.detail = "Kernel module is missing; the current rules do not use it";
            }
            issues.push_back(std::move(issue));
        } else if (check.kind == "capability") {
            auto issue = make_issue(check.key, severity, api::Code::KERNEL_CAPABILITY,
                                    capability_title(check.feature));
            issue.params = {{"feature", check.feature}};
            if (check.advisory) {
                issue.detail_code = api::DetailCode::CAPABILITY_FALLBACK;
                issue.detail = "Not available in the kernel; plain address rules are used instead";
            } else {
                issue.detail_code = api::DetailCode::CAPABILITY_UNSUPPORTED;
                issue.detail = "Not supported by the kernel";
            }
            issues.push_back(std::move(issue));
        } else {
            const std::string reason = check.reason.value_or(check.status);
            auto issue = make_issue(check.key, severity, api::Code::KERNEL_PROBE,
                                    humanize_feature(check.feature));
            issue.params = {{"feature", check.feature}, {"reason", reason}};
            issue.detail_code = api::DetailCode::KERNEL_CHECK_FAILED;
            issue.detail = "Kernel check failed: " + reason;
            issues.push_back(std::move(issue));
        }
    }
}

void collect_routing_issues(const RoutingHealthReport& report,
                            std::vector<api::HealthIssue>& issues) {
    if (!report.error.empty()) {
        auto issue = make_issue("routing-error", api::Severity::ERROR,
                                api::Code::ROUTING_CHECK_FAILED, "Routing checks failed");
        issue.detail = report.error;
        issues.push_back(std::move(issue));
        return;
    }

    const auto& chain = report.firewall_chain;
    if (chain.verification_state != VerificationState::unavailable) {
        if (!chain.chain_present) {
            auto issue = make_issue("firewall-chain", api::Severity::ERROR,
                                    api::Code::FIREWALL_CHAIN_MISSING,
                                    "Firewall chain is missing");
            issue.detail = chain.detail;
            issues.push_back(std::move(issue));
        } else if (!chain.prerouting_hook_present) {
            auto issue = make_issue("firewall-prerouting", api::Severity::ERROR,
                                    api::Code::FIREWALL_PREROUTING_MISSING,
                                    "Firewall chain is not hooked into PREROUTING");
            issue.detail = chain.detail;
            issues.push_back(std::move(issue));
        }
    }

    std::vector<std::string> bad_rules;
    for (const auto& rule : report.firewall_rules) {
        if (rule.status == CheckStatus::ok) continue;
        bad_rules.push_back(rule.set_name + ": " +
                            (rule.detail.empty() ? check_status_name(rule.status) : rule.detail));
    }
    if (!bad_rules.empty()) {
        const auto count = std::to_string(bad_rules.size());
        auto issue = make_issue("firewall-rules", api::Severity::ERROR, api::Code::FIREWALL_RULES,
                                "Firewall rules do not match: " + count);
        issue.params = {{"count", count}};
        set_list(issue, std::move(bad_rules));
        issues.push_back(std::move(issue));
    }

    std::vector<std::string> bad_routes;
    for (const auto& table : report.route_tables) {
        if (table.status == CheckStatus::ok) continue;
        bad_routes.push_back(keen_pbr3::format("{} (table {}): {}", table.outbound_tag,
                                               table.table_id, describe_route_mismatch(table)));
    }
    if (!bad_routes.empty()) {
        const auto count = std::to_string(bad_routes.size());
        auto issue = make_issue("route-tables", api::Severity::ERROR, api::Code::ROUTE_TABLES,
                                "Routing tables do not match: " + count);
        issue.params = {{"count", count}};
        set_list(issue, std::move(bad_routes));
        issues.push_back(std::move(issue));
    }

    std::vector<std::string> bad_policies;
    for (const auto& policy : report.policy_rules) {
        if (policy.status == CheckStatus::ok) continue;
        bad_policies.push_back(hex32(policy.fwmark) + "/" + hex32(policy.fwmask) + ": " +
                               (policy.detail.empty() ? check_status_name(policy.status)
                                                      : policy.detail));
    }
    if (!bad_policies.empty()) {
        const auto count = std::to_string(bad_policies.size());
        auto issue = make_issue("policy-rules", api::Severity::ERROR, api::Code::POLICY_RULES,
                                "Policy rules do not match: " + count);
        issue.params = {{"count", count}};
        set_list(issue, std::move(bad_policies));
        issues.push_back(std::move(issue));
    }

    // Non-blocking host configuration warnings (NAT, rp_filter on balance WANs).
    for (std::size_t index = 0; index < report.warnings.size(); ++index) {
        const auto& warning = report.warnings[index];
        const std::string code = health_warning_code_name(warning.code);
        const std::string interface =
            warning.interface.value_or(warning.outbound.value_or(""));
        auto issue = make_issue(
            "host-warning-" + code + "-" +
                warning.interface.value_or(std::to_string(index)),
            api::Severity::WARNING, api::Code::HOST_WARNING,
            host_warning_title(warning, interface));
        issue.params = {{"warning", code}, {"interface", interface}};
        issue.detail = warning.message;
        if (warning.outbound) issue.outbound = warning.outbound;
        issues.push_back(std::move(issue));
    }
}

void collect_dnsmasq_issues(const api::DnsmasqHealth& dnsmasq,
                            const std::string& setup_docs_url,
                            std::vector<api::HealthIssue>& issues) {
    if (dnsmasq.mode != api::ResolverIntegration::DNSMASQ) return;
    if (dnsmasq.state == api::State::ERROR) {
        auto issue = make_issue("dnsmasq", api::Severity::ERROR, api::Code::DNSMASQ_ERROR,
                                "dnsmasq does not serve the DNS rules");
        if (dnsmasq.error_code && *dnsmasq.error_code == "DNSMASQ_NOT_INSTALLED") {
            // `keen-pbr status` shows this text; the web UI has its own help.
            issue.detail = "dnsmasq is not installed, see " + setup_docs_url;
        } else if (dnsmasq.last_error) {
            issue.detail = dnsmasq.last_error;
        } else if (dnsmasq.repair_reason) {
            issue.detail = dnsmasq.repair_reason;
        } else if (dnsmasq.dnsmasq_alive == api::DnsmasqAlive::DEAD) {
            issue.detail_code = api::DetailCode::DNSMASQ_DEAD;
            issue.detail = "dnsmasq is not running";
        }
        issues.push_back(std::move(issue));
    } else if (dnsmasq.state == api::State::RECONCILING) {
        auto issue = make_issue("dnsmasq", api::Severity::WARNING,
                                api::Code::DNSMASQ_RECONCILING, "DNS rules are being repaired");
        issue.detail = dnsmasq.repair_reason;
        issues.push_back(std::move(issue));
    }
}

void collect_outbound_issues(const std::vector<api::RuntimeOutboundStateElement>& outbounds,
                             std::vector<api::HealthIssue>& issues) {
    for (const auto& outbound : outbounds) {
        std::vector<std::string> failing;
        for (const auto& member : outbound.interfaces) {
            if (member.status == api::RuntimeInterfaceStatusEnum::DEGRADED ||
                member.status == api::RuntimeInterfaceStatusEnum::UNAVAILABLE) {
                failing.push_back(describe_member(member));
            }
        }
        const bool is_group =
            outbound.type == api::OutboundType::URLTEST ||
            outbound.type == api::OutboundType::ICMPTEST;

        if (outbound.status == api::RuntimeOutboundStatusEnum::UNAVAILABLE ||
            outbound.status == api::RuntimeOutboundStatusEnum::DEGRADED) {
            const bool unavailable =
                outbound.status == api::RuntimeOutboundStatusEnum::UNAVAILABLE;
            auto issue = make_issue(
                "outbound-" + outbound.tag,
                unavailable ? api::Severity::ERROR : api::Severity::WARNING,
                unavailable ? api::Code::OUTBOUND_UNAVAILABLE : api::Code::OUTBOUND_DEGRADED,
                outbound.tag + (unavailable ? " is unavailable" : " is degraded"));
            issue.params = {{"tag", outbound.tag}};
            issue.outbound = outbound.tag;
            if (outbound.detail) {
                issue.detail = outbound.detail;
            } else if (is_group && !failing.empty()) {
                set_list(issue, std::move(failing));
            }
            issues.push_back(std::move(issue));
            continue;
        }

        // A healthy group can still contain failing children; routing works but
        // the user has less redundancy than configured.
        if (is_group && !failing.empty()) {
            const auto count = std::to_string(failing.size());
            auto issue = make_issue("outbound-" + outbound.tag + "-members",
                                    api::Severity::WARNING, api::Code::GROUP_MEMBERS_FAILING,
                                    outbound.tag + ": failing members: " + count);
            issue.params = {{"tag", outbound.tag}, {"count", count}};
            issue.outbound = outbound.tag;
            set_list(issue, std::move(failing));
            issues.push_back(std::move(issue));
        }
    }
}

} // namespace

std::vector<api::KernelCheckElement> collect_kernel_checks(
    const api::InterceptHealthClass& intercept, bool requested_dns, bool requested_l7) {
    std::vector<api::KernelCheckElement> checks;
    const auto& capabilities = intercept.capabilities;

    const auto add_capability = [&](const std::string& name, bool supported, bool relevant,
                                    bool advisory) {
        if (!relevant) return;
        api::KernelCheckElement check;
        check.key = "capability:" + name;
        check.kind = "capability";
        check.feature = name;
        check.status = supported ? "ok" : "unsupported";
        check.advisory = advisory;
        checks.push_back(std::move(check));
    };
    add_capability("nfqueue", capabilities.nfqueue, requested_dns, false);
    add_capability("nflog", capabilities.nflog, requested_l7, false);
    add_capability("connbytes", capabilities.connbytes, requested_l7, false);
    // addrtype serves the router-output skip rules (always built) and has a
    // `-d` address-match fallback, so it only ever warns. The extension list
    // covers it when present.
    if (!capabilities.xt_extensions) {
        add_capability("addrtype", capabilities.addrtype, true, true);
    }

    // iptables extensions of the routing rules: a missing one the current rules
    // use fails the firewall apply (error); an unused one only warns.
    for (const auto& extension :
         capabilities.xt_extensions.value_or(std::vector<api::XtExtensionHealthElement>{})) {
        const std::string kind = enum_name(extension.kind);
        api::KernelCheckElement check;
        check.key = "extension:" + kind + ":" + extension.name;
        check.kind = "extension";
        check.feature = extension.name;
        check.status = extension.available ? "ok" : "unsupported";
        check.advisory = !extension.required;
        check.extension_kind = kind;
        check.kernel_module = extension.kernel_module;
        checks.push_back(std::move(check));
    }

    for (const auto& probe :
         intercept.probes.value_or(std::vector<api::InterceptProbeFeatureElement>{})) {
        const bool own_capability = probe.feature == "nfqueue" || probe.feature == "nflog" ||
                                    probe.feature == "connbytes";
        const bool relevant =
            (probe.feature == "nfqueue" && requested_dns) ||
            ((probe.feature == "nflog" || probe.feature == "connbytes") && requested_l7) ||
            ((requested_dns || requested_l7) && !own_capability);
        if (!relevant) continue;
        api::KernelCheckElement check;
        check.key = "probe:" + probe.feature;
        check.kind = "probe";
        check.feature = probe.feature;
        check.status = enum_name(probe.status);
        check.advisory = false;
        check.reason = probe.reason;
        checks.push_back(std::move(check));
    }
    return checks;
}

std::string dnsmasq_setup_docs_url(std::string_view os_type) {
    constexpr std::string_view base = "https://keen-pbr.fyi/docs/";
    if (os_type == "keenetic" || os_type == "openwrt" || os_type == "debian") {
        return std::string(base) + "getting-started/installation/" + std::string(os_type) +
               "/#dnsmasq-install";
    }
    return std::string(base) + "configuration/dns/#do-i-need-dnsmasq";
}

api::HealthIssuesResponse collect_health_issues(const HealthIssueInputs& inputs) {
    api::HealthIssuesResponse response;
    auto& issues = response.issues;

    if (inputs.intercept) {
        response.kernel_checks = collect_kernel_checks(
            *inputs.intercept, inputs.requested_dns, inputs.requested_l7);
        collect_kernel_issues(response.kernel_checks, issues);
    }

    if (!inputs.service_running) {
        return response;
    }

    if (inputs.intercept) {
        if (inputs.intercept->enabled) {
            for (std::size_t index = 0; index < inputs.intercept->reasons.size(); ++index) {
                auto issue = make_issue("intercept-reason-" + std::to_string(index),
                                        api::Severity::WARNING, api::Code::INTERCEPT_LIMITED,
                                        "Traffic interception is limited");
                issue.detail = inputs.intercept->reasons[index];
                issues.push_back(std::move(issue));
            }
        }
        const auto warnings = inputs.intercept->warnings.value_or(std::vector<std::string>{});
        for (std::size_t index = 0; index < warnings.size(); ++index) {
            auto issue = make_issue("intercept-warning-" + std::to_string(index),
                                    api::Severity::WARNING, api::Code::INTERCEPT_WARNING,
                                    "Traffic interception warning");
            issue.detail = warnings[index];
            issues.push_back(std::move(issue));
        }
    }

    if (inputs.routing_health) collect_routing_issues(*inputs.routing_health, issues);
    if (inputs.dnsmasq) {
        collect_dnsmasq_issues(*inputs.dnsmasq, inputs.dnsmasq_setup_docs_url, issues);
    }
    if (inputs.outbounds) collect_outbound_issues(*inputs.outbounds, issues);
    if (inputs.doh_ca_missing) {
        auto issue = make_issue("doh", api::Severity::ERROR, api::Code::DOH_UNAVAILABLE,
                                "DNS-over-HTTPS servers are not used");
        issue.detail_code = api::DetailCode::DOH_NO_CERTIFICATES;
        issue.detail = "no CA certificates found to verify the servers";
        issues.push_back(std::move(issue));
    }
    return response;
}

} // namespace keen_pbr3
