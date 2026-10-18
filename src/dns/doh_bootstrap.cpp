#include "doh_bootstrap.hpp"

#include "../log/logger.hpp"
#include "dns_client.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>

namespace keen_pbr3 {

namespace {

constexpr std::chrono::milliseconds kLookupTimeout{3000};

} // namespace

DohBootstrap::DohBootstrap(std::function<void()> on_ready, Resolver resolver)
    : on_ready_(std::move(on_ready)), resolver_(std::move(resolver)) {}

DohBootstrap::~DohBootstrap() {
    stop();
}

void DohBootstrap::set_servers(std::vector<DnsBalancerUpstream> servers) {
    std::lock_guard<std::mutex> lock(mutex_);
    servers_ = std::move(servers);
}

void DohBootstrap::request(const std::string& host) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (std::find(queue_.begin(), queue_.end(), host) != queue_.end()) return;
        queue_.push_back(host);
        if (!thread_.joinable()) {
            thread_ = std::thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

std::vector<DohBootstrap::Result> DohBootstrap::take_results() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Result> out;
    out.swap(results_);
    return out;
}

void DohBootstrap::stop() {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        to_join = std::move(thread_);
    }
    wake_.notify_all();
    if (to_join.joinable()) to_join.join();
}

void DohBootstrap::run() {
    for (;;) {
        std::string host;
        std::vector<DnsBalancerUpstream> servers;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            host = std::move(queue_.front());
            queue_.pop_front();
            servers = servers_;
        }
        Result result;
        result.host = host;
        try {
            result.ips = resolver_ ? resolver_(host, servers)
                                   : resolve(host, servers, &result.detail);
        } catch (const std::exception& error) {
            Logger::instance().warn("DoH: cannot look up '{}': {}", host, error.what());
        }
        if (result.ips.empty()) {
            Logger::instance().warn("DoH: cannot find the address of '{}'{}", host,
                                    result.detail.empty() ? "" : " (" + result.detail + ")");
        } else {
            Logger::instance().verbose("DoH: '{}' is {}", host, result.ips.front());
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            results_.push_back(std::move(result));
        }
        if (on_ready_) on_ready_();
    }
}

std::vector<std::string> DohBootstrap::resolve(const std::string& host,
                                               const std::vector<DnsBalancerUpstream>& servers,
                                               std::string* failures) {
    std::vector<std::string> ips;
    if (!servers.empty()) {
        std::string said;
        for (const auto& server : servers) {
            std::string outcome = "no address";
            for (const std::uint16_t qtype : {kDnsTypeA, kDnsTypeAAAA}) {
                const DnsQueryResult answer =
                    dns_query(server.ip, server.port, host, qtype, kDnsClassIN, kLookupTimeout);
                if (answer.status == DnsQueryStatus::Ok) {
                    ips.insert(ips.end(), answer.addresses.begin(), answer.addresses.end());
                } else if (qtype == kDnsTypeA) {
                    outcome = dns_query_status_name(answer.status);
                }
                if (!ips.empty()) break;  // IPv4 is enough
                // A server that does not answer or denies the name will not
                // answer the AAAA question differently.
                if (qtype == kDnsTypeA && answer.status != DnsQueryStatus::Ok &&
                    answer.status != DnsQueryStatus::NoData) {
                    break;
                }
            }
            if (!ips.empty()) return ips;
            const std::string entry = (server.name.empty() ? server.ip : server.name) + " (" + outcome + ")";
            if (said.find(entry) == std::string::npos) {
                said += (said.empty() ? "" : ", ") + entry;
            }
        }
        if (failures != nullptr) *failures = said;
        return ips;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0) {
        return ips;
    }
    std::vector<std::string> v6;
    for (const addrinfo* entry = found; entry != nullptr; entry = entry->ai_next) {
        char text[INET6_ADDRSTRLEN] = {};
        if (entry->ai_family == AF_INET) {
            const auto* addr = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            ::inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text));
            ips.emplace_back(text);
        } else if (entry->ai_family == AF_INET6) {
            const auto* addr = reinterpret_cast<const sockaddr_in6*>(entry->ai_addr);
            ::inet_ntop(AF_INET6, &addr->sin6_addr, text, sizeof(text));
            v6.emplace_back(text);
        }
    }
    ::freeaddrinfo(found);
    ips.insert(ips.end(), v6.begin(), v6.end());
    return ips;
}

} // namespace keen_pbr3
