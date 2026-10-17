#pragma once

#include "dns_balancer.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace keen_pbr3 {

// Finds the addresses of the host names in DoH URLs on a worker thread, so the
// forwarder loop never waits for a lookup. The lookup goes to one plain DNS
// server when there is one (never the forwarder itself), else to the system
// resolver.
class DohBootstrap {
public:
    struct Result {
        std::string host;
        std::vector<std::string> ips;  // empty: not found
    };
    using Resolver = std::function<std::vector<std::string>(
        const std::string& host, const std::optional<DnsBalancerUpstream>& server)>;

    // `on_ready` runs on the worker thread after results were queued.
    explicit DohBootstrap(std::function<void()> on_ready, Resolver resolver = {});
    ~DohBootstrap();
    DohBootstrap(const DohBootstrap&) = delete;
    DohBootstrap& operator=(const DohBootstrap&) = delete;

    void set_server(std::optional<DnsBalancerUpstream> server);
    // Queues a lookup unless one for the host is already waiting.
    void request(const std::string& host);
    std::vector<Result> take_results();
    void stop();

    // The default resolver: dns_query() to `server`, else getaddrinfo().
    static std::vector<std::string> resolve(const std::string& host,
                                            const std::optional<DnsBalancerUpstream>& server);

private:
    void run();

    std::function<void()> on_ready_;
    Resolver resolver_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::vector<Result> results_;
    std::optional<DnsBalancerUpstream> server_;
    bool stopping_{false};
    std::thread thread_;
};

} // namespace keen_pbr3
