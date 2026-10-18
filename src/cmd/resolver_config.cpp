#include "resolver_config.hpp"

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../util/time_utils.hpp"

#include <unistd.h>

#include <fstream>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

namespace {

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) out += ", ";
        out += item;
    }
    return out;
}

// ":5380 -> [DoH] url via vpn, [DNS] 1.1.1.1; :5381 -> ..." for the syslog.
std::string describe_dns_proxies(const std::vector<DnsBalancerGroup>& groups) {
    std::string out;
    for (const auto& group : groups) {
        std::vector<std::string> upstreams;
        for (const auto& upstream : group.upstreams) {
            if (upstream.kind == DnsBalancerUpstreamKind::Doh) {
                upstreams.push_back("[DoH] " + upstream.url +
                                    (upstream.detour.empty() ? "" : " via " + upstream.detour));
            } else {
                upstreams.push_back("[DNS] " + upstream.ip +
                                    (upstream.port == 53 ? "" : "#" + std::to_string(upstream.port)));
            }
        }
        if (!out.empty()) out += "; ";
        out += ":" + std::to_string(group.port) + " -> " + join(upstreams);
    }
    return out;
}

void write_stamp(std::ostream& out, const std::string& hash) {
    write_dnsmasq_config_stamp(
        out, {hash, boottime_now_ms(), unix_timestamp_now_seconds()});
}

// The output while keen-pbr does not manage dnsmasq.
void write_fallback_output(std::ostream& out, const std::string& fallback_path,
                           const std::string& comment) {
    auto& log = Logger::instance();
    out << "# keen-pbr: " << comment << '\n';
    const std::size_t servers = write_dnsmasq_fallback_servers(out, fallback_path);
    log.info("generate-resolver-config: printing {} server(s) from {} ({})", servers,
             fallback_path, comment);
    write_stamp(out, std::string(kDnsmasqOffStampHash));
    out.flush();
}

std::string describe_failure() {
    try {
        throw;
    } catch (const ConfigValidationError& e) {
        // what() repeats the first issue; list each issue once with its path.
        std::string message;
        for (const auto& issue : e.issues()) {
            if (!message.empty()) message += "; ";
            message += issue.path + ": " + issue.message;
        }
        return message.empty() ? std::string(e.what()) : message;
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

} // namespace

int run_generate_resolver_config(const std::string& resolver_type,
                                 const std::string& config_path,
                                 const std::string& fallback_path,
                                 std::ostream& out) {
    if (resolver_type != "dnsmasq") {
        out << "# keen-pbr: unsupported resolver type\n";
        out.flush();
        return 0;
    }
    auto& log = Logger::instance();
    try {
        const Config config = [&config_path] {
            std::ifstream config_stream(config_path);
            if (!config_stream.is_open()) {
                throw std::runtime_error("Cannot open config file: " + config_path);
            }
            return parse_config(config_stream);
        }();
        validate_config(config, ConfigValidationMode::ResolverGeneration);
        if (effective_resolver_integration(config) == ResolverIntegrationMode::NONE) {
            write_fallback_output(out, fallback_path,
                                  "dnsmasq management is switched off");
            return 0;
        }
        const auto dns = config.dns.value_or(DnsConfig{});
        const auto lists = config.lists.value_or(std::map<std::string, ListConfig>{});
        const auto cache_dir = config.daemon.value_or(DaemonConfig{})
                                   .cache_dir.value_or("/var/cache/keen-pbr");
        // CacheManager's constructor does not touch the disk; only cached
        // lists are read and nothing is downloaded.
        CacheManager cache(cache_dir, max_file_size_bytes(config));
        ListStreamer streamer(cache);
        const DnsServerRegistry registry(dns);
        DnsmasqGenerator generator(registry, streamer, dns, lists);
        // Who ran us: dnsmasq (conf-script), the daemon, or a shell. Inside
        // the dnsmasq jail the PIDs are the jail's own (dnsmasq is 1) and
        // /proc may be missing, hence the "?" fallback.
        const auto parent_name = [] {
            std::ifstream comm("/proc/" + std::to_string(::getppid()) + "/comm");
            std::string name;
            return std::getline(comm, name) && !name.empty() ? name : std::string("?");
        }();
        log.info("generate-resolver-config dnsmasq: pid {}, parent {} ({}), "
                 "config {}, list cache {}",
                 static_cast<long>(::getpid()), static_cast<long>(::getppid()),
                 parent_name, config_path, cache_dir);
        if (registry.balancer_groups().empty()) {
            log.info("generate-resolver-config: no dns proxies");
        } else {
            log.info("generate-resolver-config: dns proxies on {}: {}", registry.proxy_address(),
                     describe_dns_proxies(registry.balancer_groups()));
        }
        DnsmasqGenStats stats;
        const std::string hash = generator.generate(out, &stats);
        // Runs as dnsmasq's conf-script: the log goes to syslog there.  What
        // each server= row points at is in the config comments, so only the
        // proxies, problems and a summary are logged.
        if (stats.fallback.empty()) {
            // No default upstream of our own: dnsmasq also gets the servers of
            // dnsmasq-fallback.conf (appended after the hashed part).
            out << "# keen-pbr: no default DNS servers, using " << fallback_path << '\n';
            const std::size_t servers = write_dnsmasq_fallback_servers(out, fallback_path);
            log.info("generate-resolver-config: no default upstreams; {} server(s) from {}",
                     servers, fallback_path);
        }
        for (const auto& list : stats.lists) {
            if (!list.skipped.empty()) {
                log.warn("generate-resolver-config: list '{}' skipped: {}", list.name,
                         list.skipped);
                continue;
            }
            if (list.url_pending) {
                log.warn("generate-resolver-config: list '{}': its URL source is "
                         "not downloaded yet, its domains are missing from this "
                         "dnsmasq config",
                         list.name);
            }
        }
        log.info("generate-resolver-config: done, {} rules, {} lists, {} "
                 "domains, config hash {}",
                 stats.rules, stats.lists.size(), stats.domains, hash);
        // The stamp is written after (and not covered by) the hash, so the
        // daemon can compute the same hash in memory.
        write_stamp(out, hash);
        out.flush();
    } catch (...) {
        std::string message = describe_failure();
        // dnsmasq discards stderr, so the failure goes through the logger
        // (syslog there, the terminal when run by hand).
        log.error("generate-resolver-config failed: {}", message);
        for (char& c : message) {
            if (c == '\n' || c == '\r') c = ' ';
        }
        write_fallback_output(out, fallback_path, "generate-resolver-config failed: " + message);
    }
    return 0;
}

} // namespace keen_pbr3
