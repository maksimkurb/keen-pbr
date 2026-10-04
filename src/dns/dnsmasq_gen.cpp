#include "dnsmasq_gen.hpp"
#include "../crypto/md5.hpp"
#include "../log/logger.hpp"

#include <charconv>
#include <functional>
#include <streambuf>
#include <vector>

namespace keen_pbr3 {

namespace {

static constexpr size_t kBatchSize = 50;
static constexpr size_t kMaxDnsmasqRowLength = 1024;
static constexpr size_t kMaxDomainNameLength = 255;
static constexpr size_t kRebindPrefixLen = sizeof("rebind-domain-ok=") - 1;
static constexpr size_t kServerPrefixLen = sizeof("server=") - 1;

// Forwards everything to the wrapped stream buffer while feeding the same
// bytes into an MD5 state, so the hash covers exactly what was written.
class HashingStreambuf : public std::streambuf {
public:
    explicit HashingStreambuf(std::streambuf* target) : target_(target) {}

    std::string hex_digest() const { return crypto::digest_to_hex(md5_.digest()); }

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        return xsputn(&c, 1) == 1 ? ch : traits_type::eof();
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        const std::streamsize written = target_->sputn(data, count);
        if (written > 0) {
            md5_.update(reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(written));
        }
        return written;
    }

private:
    std::streambuf* target_;
    crypto::detail::MD5State md5_;
};

std::string server_address(const DnsServerConfig& server) {
    std::string addr = server.resolved_ip;
    if (server.port != 53) {
        addr += "#" + std::to_string(server.port);
    }
    return addr;
}

} // anonymous namespace

DnsmasqGenerator::DnsmasqGenerator(const DnsServerRegistry& dns_registry,
                                   ListStreamer& list_streamer,
                                   const DnsConfig& dns_config,
                                   const std::map<std::string, ListConfig>& lists)
    : dns_registry_(dns_registry),
      list_streamer_(list_streamer),
      dns_config_(dns_config),
      lists_(lists) {}

std::string DnsmasqGenerator::generate(std::ostream& sink, DnsmasqGenStats* stats) {
    HashingStreambuf hashing(sink.rdbuf());
    std::ostream out(&hashing);
    DnsmasqGenStats local_stats;

    out << "# keen-pbr generated, do not edit\n\n";
    out << "address=/use-application-dns.net/\n\n";

    // Default upstreams: dnsmasq must not read /etc/resolv.conf when keen-pbr
    // owns the list of unscoped servers.
    const auto fallback_tags = dns_config_.fallback.value_or(std::vector<std::string>{});
    if (!fallback_tags.empty()) {
        out << "no-resolv\n";
        for (const auto& tag : fallback_tags) {
            for (const DnsServerConfig* server : dns_registry_.get_servers(tag)) {
                out << "server=" << server_address(*server) << "\n";
            }
        }
        out << "\n";
    }

    // The first enabled rule that mentions a list decides its upstream.
    std::map<std::string, std::string> dns_list_servers;
    std::map<std::string, bool> dns_list_allow_rebind;
    for (const auto& rule : dns_config_.rules.value_or(std::vector<DnsRule>{})) {
        if (!dns_rule_enabled(rule)) {
            continue;
        }
        ++local_stats.rules;
        for (const auto& list_name : rule.list) {
            if (dns_list_servers.find(list_name) == dns_list_servers.end()) {
                dns_list_servers[list_name] = rule.server;
                dns_list_allow_rebind[list_name] =
                    rule.allow_domain_rebinding.value_or(false);
            }
        }
    }

    for (const auto& [list_name, server_tag] : dns_list_servers) {
        auto list_cfg_it = lists_.find(list_name);
        if (list_cfg_it == lists_.end()) {
            continue;
        }

        const auto dns_servers = dns_registry_.get_servers(server_tag);
        const bool allow_domain_rebinding = dns_list_allow_rebind[list_name];
        if (dns_servers.empty() && !allow_domain_rebinding) {
            continue;
        }

        bool wrote_list_header = false;

        struct BatchState {
            std::string directive_name;
            size_t prefix_len{0};
            size_t suffix_len{0};
            size_t count{0};
            std::string domain_path;
            std::function<void(std::ostream&, const std::string&)> emit_line;
        };

        auto flush_batch = [&](BatchState& batch) {
            if (batch.count == 0) {
                return;
            }
            batch.emit_line(out, batch.domain_path);
            batch.domain_path.clear();
            batch.count = 0;
        };

        auto push_batch = [&](BatchState& batch, std::string_view domain) {
            std::string next_chunk = batch.domain_path;
            next_chunk += "/";
            next_chunk += domain;
            if (batch.count >= kBatchSize ||
                batch.prefix_len + next_chunk.size() + batch.suffix_len > kMaxDnsmasqRowLength) {
                flush_batch(batch);
                next_chunk.assign("/");
                next_chunk += domain;
                if (batch.prefix_len + next_chunk.size() + batch.suffix_len >
                    kMaxDnsmasqRowLength) {
                    Logger::instance().warn(
                        "Skipping domain '{}' from list '{}': {} directive would exceed {} chars",
                        domain, list_name, batch.directive_name, kMaxDnsmasqRowLength);
                    return;
                }
            }

            batch.domain_path = std::move(next_chunk);
            ++batch.count;
        };

        BatchState rebind_batch;
        rebind_batch.directive_name = "rebind-domain-ok";
        rebind_batch.prefix_len = kRebindPrefixLen;
        rebind_batch.suffix_len = 1;
        rebind_batch.emit_line = [](std::ostream& stream, const std::string& domain_path) {
            stream << "rebind-domain-ok=" << domain_path << "/\n";
        };

        std::vector<BatchState> server_batches;
        server_batches.reserve(dns_servers.size());
        for (const DnsServerConfig* server : dns_servers) {
            const std::string server_addr = server_address(*server);
            BatchState server_batch;
            server_batch.directive_name = "server";
            server_batch.prefix_len = kServerPrefixLen;
            server_batch.suffix_len = 1 + server_addr.size();
            server_batch.emit_line =
                [server_addr](std::ostream& stream, const std::string& domain_path) {
                    stream << "server=" << domain_path << "/" << server_addr << "\n";
                };
            server_batches.push_back(std::move(server_batch));
        }

        size_t list_domains = 0;
        FunctionalVisitor collector([&](EntryType type, std::string_view entry) {
            if (type != EntryType::Domain) {
                return;
            }

            std::string bare = strip_wildcard(std::string(entry));
            if (bare.empty()) {
                return;
            }
            if (bare.size() > kMaxDomainNameLength) {
                Logger::instance().warn(
                    "Skipping invalid domain '{}' from list '{}': length {} exceeds {}",
                    bare, list_name, bare.size(), kMaxDomainNameLength);
                return;
            }

            if (!wrote_list_header) {
                out << "# List: " << list_name << "\n";
                wrote_list_header = true;
            }
            ++list_domains;
            if (allow_domain_rebinding) {
                push_batch(rebind_batch, bare);
            }
            for (auto& server_batch : server_batches) {
                push_batch(server_batch, bare);
            }
        });
        list_streamer_.stream_list_preferring_cache(list_name, list_cfg_it->second, collector);

        flush_batch(rebind_batch);
        for (auto& server_batch : server_batches) {
            flush_batch(server_batch);
        }
        if (wrote_list_header) {
            out << "\n";
        }
        local_stats.domains += list_domains;
    }

    out.flush();
    if (stats != nullptr) {
        *stats = local_stats;
    }
    return hashing.hex_digest();
}

void write_dnsmasq_config_stamp(std::ostream& out, const DnsmasqConfigStamp& stamp) {
    out << "txt-record=" << kDnsmasqStampDomain << ',' << stamp.hash << '|'
        << stamp.boottime_ms << '|' << stamp.unix_ts << '\n';
}

std::optional<DnsmasqConfigStamp> parse_dnsmasq_config_stamp(std::string_view txt) {
    constexpr size_t kHashLen = 32;
    if (txt.size() < kHashLen + 1 || txt[kHashLen] != '|') {
        return std::nullopt;
    }
    for (size_t i = 0; i < kHashLen; ++i) {
        const char c = txt[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return std::nullopt;
        }
    }
    DnsmasqConfigStamp stamp;
    stamp.hash = std::string(txt.substr(0, kHashLen));

    const std::string_view rest = txt.substr(kHashLen + 1);
    const size_t bar = rest.find('|');
    if (bar == std::string_view::npos) {
        return std::nullopt;
    }
    const auto parse_int = [](std::string_view text, std::int64_t& value) {
        if (text.empty() || text.front() == '+') {
            return false;
        }
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        return ec == std::errc{} && end == text.data() + text.size() && value >= 0;
    };
    if (!parse_int(rest.substr(0, bar), stamp.boottime_ms) ||
        !parse_int(rest.substr(bar + 1), stamp.unix_ts)) {
        return std::nullopt;
    }
    return stamp;
}

std::string DnsmasqGenerator::strip_wildcard(const std::string& domain) {
    if (domain.size() > 2 && domain[0] == '*' && domain[1] == '.') {
        return domain.substr(2);
    }
    return domain;
}

} // namespace keen_pbr3
