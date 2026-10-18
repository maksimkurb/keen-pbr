#include "dns_check.hpp"

#include "dns_client.hpp"
#include "dns_server.hpp"
#include "dns_wire.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace keen_pbr3 {

namespace {

constexpr const char* kDefaultDomain = "yandex.ru";

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::vector<std::string> string_list(const nlohmann::json& body, const char* key) {
    std::vector<std::string> out;
    const auto found = body.find(key);
    if (found == body.end() || found->is_null()) return out;
    if (!found->is_array()) {
        throw DnsCheckRequestError(std::string("'") + key + "' must be an array of strings");
    }
    for (const auto& item : *found) {
        if (!item.is_string()) {
            throw DnsCheckRequestError(std::string("'") + key + "' must be an array of strings");
        }
        out.push_back(item.get<std::string>());
    }
    return out;
}

void add_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

std::string address_text(std::uint8_t family, const std::uint8_t* addr) {
    char buffer[INET6_ADDRSTRLEN] = {};
    ::inet_ntop(family == 6 ? AF_INET6 : AF_INET, addr, buffer, sizeof(buffer));
    return buffer;
}

// The status name for a reply that never came.
const char* status_for_outcome(DnsUpstreamOutcome outcome) {
    switch (outcome) {
        case DnsUpstreamOutcome::Timeout: return "timeout";
        case DnsUpstreamOutcome::ConnectionRefused: return "connection_refused";
        case DnsUpstreamOutcome::Unreachable: return "unreachable";
        case DnsUpstreamOutcome::ConnectionClosed: return "connection_closed";
        case DnsUpstreamOutcome::TlsError: return "tls_error";
        case DnsUpstreamOutcome::HttpError: return "http_error";
        case DnsUpstreamOutcome::BadAnswer: return "bad_answer";
        case DnsUpstreamOutcome::NotResolved: return "not_resolved";
        default: return "network_error";
    }
}

std::string endpoint_address(const DnsServerConfig& server) {
    if (server.is_doh()) return server.doh_url;
    const bool v6 = server.resolved_ip.find(':') != std::string::npos;
    std::string text = v6 && server.port != 53 ? "[" + server.resolved_ip + "]" : server.resolved_ip;
    if (server.port != 53) text += ":" + std::to_string(server.port);
    return text;
}

const char* server_type_name(api::DnsServerType type) {
    switch (type) {
        case api::DnsServerType::DOH: return "doh";
        case api::DnsServerType::KEENETIC: return "keenetic";
        case api::DnsServerType::STATIC: break;
    }
    return "static";
}

} // namespace

const char* dns_check_severity_name(DnsCheckSeverity severity) {
    switch (severity) {
        case DnsCheckSeverity::Ok: return "ok";
        case DnsCheckSeverity::Warning: return "warning";
        case DnsCheckSeverity::Error: return "error";
    }
    return "error";
}

bool is_valid_dns_check_domain(const std::string& domain) {
    if (domain.empty() || domain.size() > 253) return false;
    std::size_t label = 0;
    for (std::size_t i = 0; i < domain.size(); ++i) {
        const char c = domain[i];
        if (c == '.') {
            if (label == 0 || domain[i - 1] == '-') return false;
            label = 0;
            continue;
        }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '-';
        if (!ok || (c == '-' && label == 0)) return false;
        ++label;
        if (label > 63) return false;
    }
    return label != 0 && domain.back() != '-';
}

DnsCheckRequest parse_dns_check_request(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw DnsCheckRequestError("the request body must be a JSON object");
    }
    DnsCheckRequest request;
    const bool has_domains = body.contains("domains") && !body.at("domains").is_null();
    for (auto& domain : string_list(body, "domains")) {
        add_unique(request.domains, lower(std::move(domain)));
    }
    for (auto& domain : string_list(body, "custom_domains")) {
        add_unique(request.custom_domains, lower(std::move(domain)));
    }
    request.servers = string_list(body, "servers");
    // A custom domain on its own is asked alone; no list at all: the default one.
    if (!has_domains && request.custom_domains.empty()) {
        request.domains.push_back(kDefaultDomain);
    }
    // A domain named in both lists is one the user wants judged strictly.
    request.custom_domains.erase(
        std::remove_if(request.custom_domains.begin(), request.custom_domains.end(),
                       [&](const std::string& domain) {
                           return std::find(request.domains.begin(), request.domains.end(),
                                            domain) != request.domains.end();
                       }),
        request.custom_domains.end());
    if (request.domains.size() + request.custom_domains.size() > kDnsCheckMaxDomains) {
        throw DnsCheckRequestError("at most " + std::to_string(kDnsCheckMaxDomains) +
                                   " domains can be checked at once");
    }
    if (request.domains.empty() && request.custom_domains.empty()) {
        throw DnsCheckRequestError("no domains to check");
    }
    for (const auto* list : {&request.domains, &request.custom_domains}) {
        for (const auto& domain : *list) {
            if (!is_valid_dns_check_domain(domain)) {
                throw DnsCheckRequestError("'" + domain + "' is not a valid domain name");
            }
        }
    }
    return request;
}

bool is_dns_check_stub_address(std::uint8_t family, const std::uint8_t* addr, bool null_route_only) {
    if (family == 6) {
        const bool zero_prefix = std::all_of(addr, addr + 15, [](std::uint8_t b) { return b == 0; });
        if (zero_prefix && (addr[15] == 0 || addr[15] == 1)) return true;  // :: and ::1
        if (null_route_only) return false;
        return (addr[0] & 0xFE) == 0xFC ||                       // fc00::/7
               (addr[0] == 0xFE && (addr[1] & 0xC0) == 0x80);    // fe80::/10
    }
    if (addr[0] == 0 || addr[0] == 127) return true;
    if (null_route_only) return false;
    return addr[0] == 10 || (addr[0] == 172 && (addr[1] & 0xF0) == 16) ||
           (addr[0] == 192 && addr[1] == 168) || (addr[0] == 169 && addr[1] == 254) ||
           (addr[0] == 100 && (addr[1] & 0xC0) == 64);
}

DnsCheckResult classify_dns_check_answer(const std::string& domain, bool custom,
                                         const DnsProbeResult& probe) {
    DnsCheckResult result;
    result.domain = domain;
    result.duration_ms = static_cast<double>(probe.duration.count()) / 1000.0;
    if (!dns_upstream_outcome_answered(probe.outcome)) {
        result.status = status_for_outcome(probe.outcome);
        result.severity = DnsCheckSeverity::Error;
        if (!probe.error.empty()) result.error = probe.error;
        return result;
    }
    dns_wire::ParsedResponse parsed;
    if (!dns_wire::parse_response(ByteView(probe.answer.data(), probe.answer.size()), parsed)) {
        result.status = "bad_answer";
        result.severity = DnsCheckSeverity::Error;
        result.error = "the answer is not a DNS message";
        return result;
    }
    result.rcode = parsed.rcode;
    bool stub = false;
    for (const auto& record : parsed.addresses) {
        result.addresses.push_back(address_text(record.family, record.addr.data()));
        stub = stub || is_dns_check_stub_address(record.family, record.addr.data(), custom);
    }
    switch (parsed.rcode) {
        case 0:
            if (stub) {
                result.status = "stub_address";
                result.severity = DnsCheckSeverity::Warning;
            } else if (result.addresses.empty()) {
                // A domain of the user may have no IPv4 address at all.
                result.status = "nodata";
                result.severity = custom ? DnsCheckSeverity::Ok : DnsCheckSeverity::Warning;
            } else {
                result.status = "ok";
            }
            break;
        case 3:
            // The user asks about a domain of theirs to see whether it is
            // blocked, so NXDOMAIN is worth a look either way.
            result.status = "nxdomain";
            result.severity = DnsCheckSeverity::Warning;
            break;
        case 2:
            result.status = "servfail";
            result.severity = DnsCheckSeverity::Error;
            break;
        case 5:
            result.status = "refused";
            result.severity = DnsCheckSeverity::Error;
            break;
        default:
            result.status = "rcode";
            result.severity = DnsCheckSeverity::Error;
            result.error = "response code " + std::to_string(parsed.rcode);
            break;
    }
    return result;
}

DnsCheckSeverity dns_check_overall(const std::vector<DnsCheckServer>& servers) {
    DnsCheckSeverity overall = DnsCheckSeverity::Ok;
    const auto raise = [&](DnsCheckSeverity severity) {
        overall = std::max(overall, severity);
    };
    for (const auto& server : servers) {
        if (server.error) raise(DnsCheckSeverity::Error);
        for (const auto& endpoint : server.endpoints) {
            if (endpoint.status == "unavailable") raise(DnsCheckSeverity::Error);
            for (const auto& result : endpoint.results) raise(result.severity);
        }
    }
    return overall;
}

nlohmann::json dns_check_report_to_json(const DnsCheckReport& report) {
    nlohmann::json servers = nlohmann::json::array();
    for (const auto& server : report.servers) {
        nlohmann::json endpoints = nlohmann::json::array();
        for (const auto& endpoint : server.endpoints) {
            nlohmann::json results = nlohmann::json::array();
            for (const auto& result : endpoint.results) {
                nlohmann::json item = {{"domain", result.domain},
                                       {"status", result.status},
                                       {"severity", dns_check_severity_name(result.severity)},
                                       {"addresses", result.addresses},
                                       {"duration_ms", result.duration_ms}};
                if (result.rcode) item["rcode"] = *result.rcode;
                if (result.error) item["error"] = *result.error;
                results.push_back(std::move(item));
            }
            nlohmann::json item = {{"address", endpoint.address},
                                   {"status", endpoint.status},
                                   {"results", std::move(results)}};
            if (endpoint.reason_code) item["reason_code"] = *endpoint.reason_code;
            if (endpoint.reason) item["reason"] = *endpoint.reason;
            if (!endpoint.ips.empty()) item["ips"] = endpoint.ips;
            endpoints.push_back(std::move(item));
        }
        nlohmann::json item = {{"tag", server.tag},
                               {"type", server.type},
                               {"endpoints", std::move(endpoints)}};
        if (server.detour) item["detour"] = *server.detour;
        if (server.error_code) item["error_code"] = *server.error_code;
        if (server.error) item["error"] = *server.error;
        servers.push_back(std::move(item));
    }
    return {{"overall", dns_check_severity_name(report.overall)}, {"servers", std::move(servers)}};
}

namespace {

// The plain upstreams of one server of dns.servers, named by its tag.
std::vector<DnsBalancerUpstream> plain_upstreams(const DnsServer& configured,
                                                 const DnsCheckInput& input) {
    std::vector<DnsBalancerUpstream> found;
    const auto type = configured.type.value_or(api::DnsServerType::STATIC);
    try {
        std::vector<std::string> addresses;
        if (type == api::DnsServerType::KEENETIC) {
            if (input.keenetic_addresses) addresses = input.keenetic_addresses();
        } else if (type == api::DnsServerType::STATIC) {
            addresses.push_back(configured.address.value_or(""));
        }
        for (const auto& address : addresses) {
            const DnsServerConfig parsed = parse_dns_server(configured.tag, address, configured.detour);
            found.push_back(DnsBalancerUpstream::plain(parsed.resolved_ip, parsed.port));
            found.back().name = configured.tag;
        }
    } catch (const std::exception&) {
        found.clear();  // the server cannot be used to look names up
    }
    return found;
}

// The servers that look up the host names of DoH URLs: dns.bootstrap, else
// the first plain server of dns.servers.
std::vector<DnsBalancerUpstream> bootstrap_upstreams(const DnsCheckInput& input) {
    std::vector<DnsBalancerUpstream> found;
    for (const auto& tag : input.bootstrap) {
        for (const auto& server : input.servers) {
            if (server.tag != tag) continue;
            auto upstreams = plain_upstreams(server, input);
            found.insert(found.end(), upstreams.begin(), upstreams.end());
        }
    }
    if (!input.bootstrap.empty()) return found;
    for (const auto& server : input.servers) {
        auto upstreams = plain_upstreams(server, input);
        if (!upstreams.empty()) return {upstreams.front()};
    }
    return found;
}

} // namespace

DnsCheckReport run_dns_check(const DnsCheckInput& input, const DnsCheckProbeFn& probe) {
    const DnsCheckRequest& request = input.request;
    std::set<std::string> known;
    for (const auto& server : input.servers) known.insert(server.tag);
    for (const auto& tag : request.servers) {
        if (known.count(tag) == 0) {
            throw DnsCheckRequestError("unknown DNS server '" + tag + "'");
        }
    }

    DnsCheckReport report;
    // What to ask: the endpoint (server, endpoint index) and the question.
    struct Question {
        std::size_t server;
        std::size_t endpoint;
        std::size_t domain;  // index into `names`
    };
    std::vector<std::string> names;
    std::vector<bool> custom_flags;
    for (const auto& domain : request.domains) {
        names.push_back(domain);
        custom_flags.push_back(false);
    }
    for (const auto& domain : request.custom_domains) {
        names.push_back(domain);
        custom_flags.push_back(true);
    }

    std::vector<Question> questions;
    std::vector<DnsProbeRequest> requests;
    bool needs_bootstrap = false;
    std::uint16_t next_id = 1;
    const auto ask = [&](std::size_t server, std::size_t endpoint, std::size_t domain,
                         const DnsBalancerUpstream& upstream) {
        DnsProbeRequest wanted;
        wanted.upstream = upstream;
        wanted.query = build_dns_query(next_id++, names[domain], kDnsTypeA, kDnsClassIN, true);
        requests.push_back(std::move(wanted));
        questions.push_back({server, endpoint, domain});
    };

    for (const auto& configured : input.servers) {
        if (!request.servers.empty() &&
            std::find(request.servers.begin(), request.servers.end(), configured.tag) ==
                request.servers.end()) {
            continue;
        }
        DnsCheckServer server;
        server.tag = configured.tag;
        const auto type = configured.type.value_or(api::DnsServerType::STATIC);
        server.type = server_type_name(type);
        if (configured.detour && !configured.detour->empty()) server.detour = *configured.detour;

        std::vector<DnsServerConfig> parsed;
        try {
            if (type == api::DnsServerType::KEENETIC) {
                std::vector<std::string> addresses;
                try {
                    addresses = input.keenetic_addresses ? input.keenetic_addresses()
                                                         : std::vector<std::string>{};
                } catch (const std::exception& error) {
                    server.error_code = "keenetic_dns_unavailable";
                    server.error = error.what();
                }
                if (!server.error && addresses.empty()) {
                    server.error_code = "keenetic_dns_unavailable";
                    server.error = "Keenetic reports no DNS servers";
                }
                for (const auto& address : addresses) {
                    parsed.push_back(parse_dns_server(configured.tag, address, configured.detour));
                }
            } else if (type == api::DnsServerType::DOH) {
                parsed.push_back(parse_doh_server(configured.tag, configured.address.value_or(""),
                                                  configured.detour,
                                                  configured.ips.value_or(std::vector<std::string>{})));
            } else {
                parsed.push_back(
                    parse_dns_server(configured.tag, configured.address.value_or(""), configured.detour));
            }
        } catch (const DnsError& error) {
            server.error_code = "invalid_address";
            server.error = error.what();
            parsed.clear();
        }

        const std::size_t server_index = report.servers.size();
        for (const auto& address : parsed) {
            DnsCheckEndpoint endpoint;
            endpoint.address = endpoint_address(address);
            endpoint.ips = address.ips;
            const std::size_t endpoint_index = server.endpoints.size();
            if (!address.is_doh() && !input.ipv6_enabled &&
                address.resolved_ip.find(':') != std::string::npos) {
                endpoint.status = "skipped";
                endpoint.reason_code = "ipv6_disabled";
                endpoint.reason = "IPv6 is disabled";
            } else if (address.is_doh() && input.doh_unavailable) {
                endpoint.status = "unavailable";
                endpoint.reason_code = input.doh_unavailable->first;
                endpoint.reason = input.doh_unavailable->second;
            } else {
                DnsBalancerUpstream upstream =
                    address.is_doh() ? DnsBalancerUpstream::doh(address.doh_url, address.detour.value_or(""),
                                                                address.ips)
                                     : DnsBalancerUpstream::plain(address.resolved_ip, address.port);
                if (address.is_doh() && address.ips.empty()) needs_bootstrap = true;
                for (std::size_t i = 0; i < names.size(); ++i) {
                    ask(server_index, endpoint_index, i, upstream);
                }
            }
            server.endpoints.push_back(std::move(endpoint));
        }
        report.servers.push_back(std::move(server));
    }

    // DoH host names are looked up through dns.bootstrap, else the first
    // plain DNS server.
    DnsBalancerSettings settings = input.settings;
    if (needs_bootstrap && settings.bootstrap.empty()) {
        settings.bootstrap = bootstrap_upstreams(input);
    }

    std::vector<DnsProbeResult> answers;
    if (!requests.empty()) {
        answers = probe(requests, kDnsCheckDeadline, settings);
    }
    answers.resize(requests.size());

    for (std::size_t i = 0; i < questions.size(); ++i) {
        const Question& question = questions[i];
        DnsCheckEndpoint& endpoint = report.servers[question.server].endpoints[question.endpoint];
        const DnsProbeResult& answer = answers[i];
        if (!answer.usable) {
            endpoint.status = "unavailable";
            endpoint.reason = answer.error;
            endpoint.results.clear();
            continue;
        }
        if (endpoint.status == "unavailable") continue;
        endpoint.results.push_back(classify_dns_check_answer(
            names[question.domain], custom_flags[question.domain], answer));
    }
    report.overall = dns_check_overall(report.servers);
    return report;
}

} // namespace keen_pbr3
