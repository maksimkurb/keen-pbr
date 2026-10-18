#include "dnsmasq_gen.hpp"
#include "../crypto/md5.hpp"
#include "../log/logger.hpp"

#include <charconv>
#include <fstream>
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

std::string no_address_reason(const std::vector<std::string>& servers) {
    std::string quoted;
    for (const auto& tag : servers) {
        if (!quoted.empty()) {
            quoted += ", ";
        }
        quoted += "'" + tag + "'";
    }
    return (servers.size() == 1 ? "DNS server " + quoted + " has"
                                : "DNS servers " + quoted + " have") +
           std::string(" no address");
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

    if (!dns_registry_.balancer_error().empty()) {
        throw DnsError(dns_registry_.balancer_error());
    }

    // Default upstreams: dnsmasq must not read /etc/resolv.conf when keen-pbr
    // owns the list of unscoped servers.  A DoH server is reached through the
    // local forwarder.
    const auto fallback_tags = dns_config_.fallback.value_or(std::vector<std::string>{});
    if (!fallback_tags.empty()) {
        const DnsTargets& fallback = dns_registry_.fallback_targets();
        out << "no-resolv\n";
        for (std::size_t i = 0; i < fallback.addresses.size(); ++i) {
            const std::string& address = fallback.addresses[i];
            if (i < fallback.notes.size()) out << "# " << fallback.notes[i] << "\n";
            out << "server=" << address << "\n";
            local_stats.fallback.push_back(address);
        }
        out << "\n";
    }

    // Every enabled rule naming a list adds its servers, in rule order and
    // without repeating one.  A list with several servers (or a DoH one) gets
    // a single server= row per domain, aimed at the local forwarder, which
    // tries the servers in turn: dnsmasq stores one record per domain and
    // server, and lists have up to ~100k domains.
    for (const auto& rule : dns_config_.rules.value_or(std::vector<DnsRule>{})) {
        if (dns_rule_enabled(rule)) {
            ++local_stats.rules;
        }
    }
    const std::map<std::string, DnsListUsage> dns_lists = collect_dns_list_usage(dns_config_);

    for (const auto& entry : dns_lists) {
        const std::string& list_name = entry.first;
        const DnsListUsage& usage = entry.second;
        const DnsTargets& targets = dns_registry_.list_targets(list_name);
        DnsmasqListStats& list_stats = local_stats.lists.emplace_back();
        list_stats.name = list_name;
        list_stats.servers = usage.servers;
        list_stats.upstreams = targets.upstreams;
        list_stats.allow_rebind = usage.allow_rebind;
        auto list_cfg_it = lists_.find(list_name);
        if (list_cfg_it == lists_.end()) {
            list_stats.skipped = "the list is not defined in lists";
            continue;
        }

        const bool allow_domain_rebinding = usage.allow_rebind;
        if (targets.addresses.empty() && !allow_domain_rebinding) {
            list_stats.skipped = no_address_reason(usage.servers);
            continue;
        }
        list_stats.url_pending = list_cfg_it->second.url.has_value() &&
                                 !list_streamer_.has_cached(list_name);

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
        server_batches.reserve(targets.addresses.size());
        for (const std::string& server_addr : targets.addresses) {
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
                out << "# List: " << list_name << "\n\n";
                for (const std::string& note : targets.notes) out << "# " << note << "\n";
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
        list_stats.domains = list_domains;
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

std::size_t write_dnsmasq_fallback_servers(std::ostream& out, const std::string& path) {
    std::ifstream in(path);
    std::size_t count = 0;
    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 7, "server=") != 0) {
            continue;
        }
        out << line.substr(first) << '\n';
        ++count;
    }
    return count;
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
