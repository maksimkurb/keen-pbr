#include "dnsmasq_gen.hpp"
#include "../crypto/md5.hpp"
#include "../log/logger.hpp"

#include <chrono>
#include <set>
#include <streambuf>

namespace keen_pbr3 {

namespace {

static constexpr const char* kDnsProbeZone = "check.keen.pbr";
static constexpr size_t kBatchSize = 50;
static constexpr size_t kMaxDnsmasqRowLength = 1024;
static constexpr size_t kMaxDomainNameLength = 255;
static constexpr size_t kIpsetPrefixLen = sizeof("ipset=") - 1;
static constexpr size_t kNftsetPrefixLen = sizeof("nftset=") - 1;
static constexpr size_t kRebindPrefixLen = sizeof("rebind-domain-ok=") - 1;
static constexpr size_t kServerPrefixLen = sizeof("server=") - 1;
static constexpr const char* kNftSetPrefix = "/4#inet#KeenPbrTable#";
static constexpr const char* kNftSetMiddle = ",6#inet#KeenPbrTable#";
static constexpr size_t kNftSetPrefixLen = sizeof("/4#inet#KeenPbrTable#") - 1;
static constexpr size_t kNftSetMiddleLen = sizeof(",6#inet#KeenPbrTable#") - 1;

// A streambuf that simultaneously forwards bytes to a sink streambuf
// and feeds them into an MD5State. No buffering of config content.
class HashingTeeStreamBuf : public std::streambuf {
    std::streambuf*           sink_;
    crypto::detail::MD5State  md5_;
public:
    explicit HashingTeeStreamBuf(std::streambuf* sink) : sink_(sink) {}

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        md5_.update(reinterpret_cast<const uint8_t*>(s), static_cast<size_t>(n));
        return sink_->sputn(s, n);
    }
    int overflow(int c) override {
        if (c == EOF) return c;
        char ch = static_cast<char>(c);
        md5_.update(reinterpret_cast<const uint8_t*>(&ch), 1);
        return sink_->sputc(ch);
    }
    std::string finalize() {
        return crypto::digest_to_hex(md5_.digest());
    }
};

template <typename EmitLineFn>
void emit_chunked_domain_lines(std::ostream& out,
                               const std::set<std::string>& domains,
                               const std::string& list_name,
                               size_t prefix_len,
                               size_t suffix_len,
                               std::string_view directive_name,
                               EmitLineFn emit_line) {
    auto it = domains.begin();
    while (it != domains.end()) {
        std::string domain_path;
        size_t count = 0;
        while (it != domains.end() && count < kBatchSize) {
            std::string next_chunk = domain_path + "/" + *it;
            if (prefix_len + next_chunk.size() + suffix_len > kMaxDnsmasqRowLength) {
                if (count == 0) {
                    Logger::instance().warn(
                        "Skipping domain '{}' from list '{}': {} directive would exceed {} chars",
                        *it, list_name, directive_name, kMaxDnsmasqRowLength);
                    ++it;
                }
                break;
            }
            domain_path = std::move(next_chunk);
            ++it;
            ++count;
        }

        if (count == 0) {
            continue;
        }

        emit_line(domain_path);
    }
}

} // anonymous namespace

DnsmasqGenerator::DnsmasqGenerator(const DnsServerRegistry& dns_registry,
                                   ListStreamer& list_streamer,
                                   const RouteConfig& route_config,
                                   const DnsConfig& dns_config,
                                   const std::map<std::string, ListConfig>& lists,
                                   ResolverType resolver_type)
    : dns_registry_(dns_registry),
      list_streamer_(list_streamer),
      route_config_(route_config),
      dns_config_(dns_config),
      lists_(lists),
      resolver_type_(resolver_type) {}

void DnsmasqGenerator::for_each_ipset_domain(
    std::function<void(const std::string& domain,
                       const std::string& list_name)> callback)
{
    // Collect all list names referenced in route rules that need ipset population.
    std::set<std::string> ipset_lists;
    for (const auto& rule : route_config_.rules.value_or(std::vector<RouteRule>{})) {
        for (const auto& list_name : rule.list) {
            ipset_lists.insert(list_name);
        }
    }

    std::set<std::string> domains;

    for (const auto& list_name : ipset_lists) {
        auto list_cfg_it = lists_.find(list_name);
        if (list_cfg_it == lists_.end()) continue;

        domains.clear();
        FunctionalVisitor collector([&](EntryType type, std::string_view entry) {
            if (type == EntryType::Domain) {
                std::string bare = strip_wildcard(std::string(entry));
                if (!bare.empty()) {
                    if (bare.size() > kMaxDomainNameLength) {
                        Logger::instance().warn(
                            "Skipping invalid domain '{}' from list '{}': length {} exceeds {}",
                            bare, list_name, bare.size(), kMaxDomainNameLength);
                        return;
                    }
                    domains.insert(std::move(bare));
                }
            }
        });
        list_streamer_.stream_list(list_name, list_cfg_it->second, collector);

        for (const auto& domain : domains) {
            callback(domain, list_name);
        }
    }
}

std::string DnsmasqGenerator::compute_config_hash() {
    struct NullBuf : std::streambuf {
        std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
        int overflow(int c) override { return c == EOF ? EOF : c; }
    } null_buf;

    HashingTeeStreamBuf tee(&null_buf);
    std::ostream hashing_out(&tee);
    generate_directives(hashing_out);
    return tee.finalize();
}

std::string DnsmasqGenerator::compute_config_hash(
    const DnsServerRegistry& dns_registry,
    ListStreamer& list_streamer,
    const RouteConfig& route_config,
    const DnsConfig& dns_config,
    const std::map<std::string, ListConfig>& lists,
    ResolverType resolver_type)
{
    return DnsmasqGenerator(dns_registry, list_streamer, route_config,
                            dns_config, lists, resolver_type)
           .compute_config_hash();
}

void DnsmasqGenerator::generate_directives(std::ostream& out) {
    const char* resolver_name = (resolver_type_ == ResolverType::DNSMASQ_IPSET)
        ? "dnsmasq-ipset" : "dnsmasq-nftset";
    out << "# Generated by keen-pbr (" << resolver_name << ") - do not edit manually\n\n";

    // Block Firefox DoH canary domain to force fallback to system resolver for better DNS rule coverage.
    // https://support.mozilla.org/en-US/kb/canary-domain-use-application-dnsnet
    out << "address=/use-application-dns.net/\n\n";

    if (dns_config_.dns_test_server.has_value()) {
        const auto parsed = parse_dns_address_str(dns_config_.dns_test_server->listen);
        out << "rebind-domain-ok=keen.pbr\n";
        out << "server=/" << kDnsProbeZone << "/" << parsed.ip << "#" << parsed.port << "\n\n";
    }

    for (const DnsServerConfig* server : dns_registry_.fallback_servers()) {
        std::string server_addr = server->resolved_ip;
        if (server->port != 53) {
            server_addr += "#" + std::to_string(server->port);
        }
        out << "server=" << server_addr << "\n";
    }

    if (dns_config_.fallback.has_value() && !dns_config_.fallback->empty()) {
        out << "\n";
    }

    // Collect all list names referenced in route rules that need ipset population.
    std::set<std::string> ipset_lists;
    for (const auto& rule : route_config_.rules.value_or(std::vector<RouteRule>{})) {
        for (const auto& list_name : rule.list) {
            ipset_lists.insert(list_name);
        }
    }

    // Collect all list names referenced in DNS rules for server directives.
    // Map: list_name -> dns server tag
    std::map<std::string, std::string> dns_list_servers;
    std::map<std::string, bool> dns_list_allow_rebind;
    for (const auto& rule : dns_config_.rules.value_or(std::vector<DnsRule>{})) {
        for (const auto& list_name : rule.list) {
            if (dns_list_servers.find(list_name) == dns_list_servers.end()) {
                dns_list_servers[list_name] = rule.server;
                dns_list_allow_rebind[list_name] =
                    rule.allow_domain_rebinding.value_or(false);
            }
        }
    }

    // Gather all unique list names that need processing
    std::set<std::string> all_lists;
    all_lists.insert(ipset_lists.begin(), ipset_lists.end());
    for (const auto& [list_name, _] : dns_list_servers) {
        all_lists.insert(list_name);
    }

    // Domain-collecting visitor with dedup set, reused across lists
    std::set<std::string> domains;

    for (const auto& list_name : all_lists) {
        // Find the list config
        auto list_cfg_it = lists_.find(list_name);
        if (list_cfg_it == lists_.end()) {
            continue;
        }

        // Collect unique domains by streaming through a visitor
        domains.clear();
        FunctionalVisitor collector([&](EntryType type, std::string_view entry) {
            if (type == EntryType::Domain) {
                std::string bare = strip_wildcard(std::string(entry));
                if (!bare.empty()) {
                    if (bare.size() > kMaxDomainNameLength) {
                        Logger::instance().warn(
                            "Skipping invalid domain '{}' from list '{}': length {} exceeds {}",
                            bare, list_name, bare.size(), kMaxDomainNameLength);
                        return;
                    }
                    domains.insert(std::move(bare));
                }
            }
        });
        list_streamer_.stream_list(list_name, list_cfg_it->second, collector);

        if (domains.empty()) continue;

        out << "# List: " << list_name << "\n";

        bool needs_ipset = ipset_lists.count(list_name) > 0;

        // Determine DNS server IP for this list (if a DNS rule maps it)
        std::string  dns_ip;
        uint16_t     dns_port = 53;
        auto dns_it = dns_list_servers.find(list_name);
        if (dns_it != dns_list_servers.end()) {
            const DnsServerConfig* server = dns_registry_.get_server(dns_it->second);
            if (server) {
                dns_ip   = server->resolved_ip;
                dns_port = server->port;
            }
        }
        const bool allow_domain_rebinding =
            dns_list_allow_rebind.find(list_name) != dns_list_allow_rebind.end()
            && dns_list_allow_rebind[list_name];
        std::string server_addr = dns_ip;
        if (!server_addr.empty() && dns_port != 53) {
            server_addr += "#" + std::to_string(dns_port);
        }

        if (needs_ipset) {
            const std::string set4 = ipset_name_v4(list_name);
            const std::string set6 = ipset_name_v6(list_name);
            if (resolver_type_ == ResolverType::DNSMASQ_IPSET) {
                const size_t suffix_len = 1 + set4.size() + 1 + set6.size();
                emit_chunked_domain_lines(
                    out, domains, list_name, kIpsetPrefixLen, suffix_len, "ipset",
                    [&](const std::string& domain_path) {
                        out << "ipset=" << domain_path << "/" << set4 << "," << set6 << "\n";
                    });
            } else {
                const size_t suffix_len = kNftSetPrefixLen + set4.size() + kNftSetMiddleLen + set6.size();
                emit_chunked_domain_lines(
                    out, domains, list_name, kNftsetPrefixLen, suffix_len, "nftset",
                    [&](const std::string& domain_path) {
                        out << "nftset=" << domain_path
                            << kNftSetPrefix << set4
                            << kNftSetMiddle << set6 << "\n";
                    });
            }
        }
        if (allow_domain_rebinding) {
            emit_chunked_domain_lines(
                out, domains, list_name, kRebindPrefixLen, 1, "rebind-domain-ok",
                [&](const std::string& domain_path) {
                    out << "rebind-domain-ok=" << domain_path << "/\n";
                });
        }
        if (!server_addr.empty()) {
            emit_chunked_domain_lines(
                out, domains, list_name, kServerPrefixLen, 1 + server_addr.size(), "server",
                [&](const std::string& domain_path) {
                    out << "server=" << domain_path << "/" << server_addr << "\n";
                });
        }

        out << "\n";
    }
}

void DnsmasqGenerator::generate(std::ostream& out) {
    HashingTeeStreamBuf tee(out.rdbuf());
    std::ostream hashing_out(&tee);
    generate_directives(hashing_out);
    const std::string hash = tee.finalize();
    const auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    out << "txt-record=config-hash.keen.pbr," << now_ts << "|" << hash << "\n";
}


ResolverType resolver_type_from_dns_config(const DnsConfig& dns_config) {
    if (!dns_config.system_resolver.has_value()) {
        return ResolverType::DNSMASQ_IPSET;
    }

    switch (dns_config.system_resolver->type) {
    case DnsSystemResolverType::DNSMASQ_IPSET:
        return ResolverType::DNSMASQ_IPSET;
    case DnsSystemResolverType::DNSMASQ_NFTSET:
        return ResolverType::DNSMASQ_NFTSET;
    }

    return ResolverType::DNSMASQ_IPSET;
}

ResolverType DnsmasqGenerator::parse_resolver_type(const std::string& s) {
    if (s == "dnsmasq-ipset")  return ResolverType::DNSMASQ_IPSET;
    if (s == "dnsmasq-nftset") return ResolverType::DNSMASQ_NFTSET;
    throw std::invalid_argument("Unknown resolver type: " + s);
}

std::string DnsmasqGenerator::strip_wildcard(const std::string& domain) {
    if (domain.size() > 2 && domain[0] == '*' && domain[1] == '.') {
        return domain.substr(2);
    }
    return domain;
}

} // namespace keen_pbr3
