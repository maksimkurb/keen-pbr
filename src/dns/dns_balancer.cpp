#include "dns_balancer.hpp"

#include "../log/logger.hpp"
#ifdef WITH_DOH
#include "doh_bootstrap.hpp"
#include "doh_client.hpp"
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>

namespace keen_pbr3 {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxListeners = kDnsBalancerMaxGroups;
constexpr std::size_t kUdpSlots = 128;
constexpr std::size_t kTcpSessions = 16;
constexpr std::size_t kProbeSlots = 64;
constexpr std::size_t kUdpQueryMax = 1500;
constexpr std::size_t kUdpAnswerMax = 16384;
constexpr std::size_t kTcpQueryMax = 4096;
constexpr std::size_t kTcpAnswerMax = 16384;
constexpr std::size_t kDnsHeaderLen = 12;
constexpr int kAcceptPerWake = 8;
constexpr int kUdpPerWake = 32;
constexpr std::size_t kNoTransfer = static_cast<std::size_t>(-1);
// A DoH host whose connections fail is looked up again, at most this often.
constexpr std::chrono::seconds kBootstrapRetry{30};

enum class Kind : std::uint32_t { Wake, ListenUdp, ListenTcp, UdpUp, TcpClient, TcpUp, Doh, ProbeUp };

std::uint64_t make_tag(Kind kind, std::size_t index) {
    return (static_cast<std::uint64_t>(kind) << 32) | static_cast<std::uint32_t>(index);
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool make_addr(const std::string& ip, std::uint16_t port, sockaddr_storage& out,
               socklen_t& out_len) {
    out = {};
    auto* v4 = reinterpret_cast<sockaddr_in*>(&out);
    if (::inet_pton(AF_INET, ip.c_str(), &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(port);
        out_len = sizeof(sockaddr_in);
        return true;
    }
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&out);
    out = {};
    if (::inet_pton(AF_INET6, ip.c_str(), &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(port);
        out_len = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

// The listen address of the forwarder (IPv4 only).
bool make_listen_addr(const std::string& ip, std::uint16_t port, sockaddr_in& out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return ::inet_pton(AF_INET, ip.c_str(), &out.sin_addr) == 1;
}

bool is_ip_literal(const std::string& host) {
    in6_addr buffer{};
    return ::inet_pton(AF_INET, host.c_str(), &buffer) == 1 ||
           ::inet_pton(AF_INET6, host.c_str(), &buffer) == 1;
}

std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

// Offset just past the question section of a query, 0 when it is malformed.
std::size_t question_end(const std::uint8_t* msg, std::size_t len) {
    if (len < kDnsHeaderLen || read_be16(msg + 4) != 1) {
        return 0;
    }
    std::size_t pos = kDnsHeaderLen;
    while (pos < len && msg[pos] != 0) {
        if ((msg[pos] & 0xC0) != 0) {
            return 0;
        }
        pos += static_cast<std::size_t>(msg[pos]) + 1;
    }
    pos += 1 + 4;  // root label, type and class
    return pos <= len ? pos : 0;
}

// SERVFAIL for `query` written to `out`; returns its length (0: not possible).
std::size_t build_servfail(const std::uint8_t* query, std::size_t len, std::uint8_t* out) {
    const std::size_t end = question_end(query, len);
    if (end == 0) {
        return 0;
    }
    std::memcpy(out, query, end);
    out[2] = static_cast<std::uint8_t>(0x80 | (query[2] & 0x79));  // QR, keep opcode and RD
    out[3] = static_cast<std::uint8_t>(0x80 | 0x02);               // RA, SERVFAIL
    out[6] = out[7] = out[8] = out[9] = out[10] = out[11] = 0;     // AN, NS, AR
    return end;
}

bool is_servfail(const std::uint8_t* msg) {
    return (msg[3] & 0x0F) == 2;
}

DnsUpstreamOutcome outcome_for_rcode(const std::uint8_t* msg) {
    switch (msg[3] & 0x0F) {
        case 0: return DnsUpstreamOutcome::Ok;
        case 2: return DnsUpstreamOutcome::Servfail;
        case 3: return DnsUpstreamOutcome::Nxdomain;
        case 5: return DnsUpstreamOutcome::Refused;
        default: return DnsUpstreamOutcome::OtherRcode;
    }
}

DnsUpstreamOutcome outcome_for_errno(int error) {
    switch (error) {
        case ECONNREFUSED: return DnsUpstreamOutcome::ConnectionRefused;
        case EHOSTUNREACH:
        case ENETUNREACH:
        case EHOSTDOWN:
        case EPERM:
        case EACCES: return DnsUpstreamOutcome::Unreachable;
        case ECONNRESET:
        case EPIPE: return DnsUpstreamOutcome::ConnectionClosed;
        case ETIMEDOUT: return DnsUpstreamOutcome::Timeout;
        default: return DnsUpstreamOutcome::NetworkError;
    }
}

// Who an upstream is, for the failure streak: the same server in several
// groups is one.
std::string upstream_identity(const DnsBalancerUpstream& spec) {
    if (spec.kind != DnsBalancerUpstreamKind::Doh) {
        return "p|" + spec.ip + "|" + std::to_string(spec.port);
    }
    std::string identity = "d|" + spec.url + "|" + spec.detour;
    for (const auto& ip : spec.ips) identity += "|" + ip;
    return identity;
}

// What an upstream was asked and answered, since it was configured.
struct UpstreamCounters {
    std::array<std::uint64_t, kDnsUpstreamOutcomeCount> results{};
    std::array<std::uint64_t, kDnsLatencyBoundsMs.size() + 1> latency_buckets{};
    std::uint64_t latency_sum_us{0};
};

// Consecutive failures of one upstream identity.
struct Streak {
    std::uint32_t count{0};
    DnsUpstreamOutcome last{DnsUpstreamOutcome::Ok};
    std::string error;
    bool failing() const { return count >= kDnsFailingStreak; }
};

struct Upstream {
    DnsBalancerUpstream spec;
    std::string identity;
    UpstreamCounters counters;
    sockaddr_storage addr{};       // plain
    socklen_t addr_len{0};
    std::size_t doh_endpoint{kNoTransfer};  // DoH: endpoint of the DoH client
    bool usable{true};             // false: never tried (no DoH support, no mark for the detour)
    Clock::time_point dead_until{};

    bool is_doh() const { return spec.kind == DnsBalancerUpstreamKind::Doh; }
};

struct Listener {
    bool used{false};
    std::uint16_t port{0};
    int udp_fd{-1};
    int tcp_fd{-1};
    std::vector<Upstream> upstreams;
    std::uint32_t next{0};
    DnsListenerStats stats;  // the counters of the listener (upstreams excluded)
};

// One in-flight UDP query: an upstream socket of its own and the query bytes.
struct UdpSlot {
    bool used{false};
    std::size_t listener{0};
    int up_fd{-1};
    std::size_t doh{kNoTransfer};  // DoH transfer in flight
    sockaddr_in client{};
    std::uint16_t len{0};
    std::array<std::uint8_t, kUdpQueryMax> query{};
    std::uint32_t start{0};
    std::uint32_t tried{0};
    std::size_t current{0};
    bool ignore_dead{false};
    Clock::time_point attempt_start{};
    Clock::time_point deadline{};
};

enum class TcpState : std::uint8_t { ReadQuery, Connect, ReadAnswer, WriteAnswer };

struct TcpSession {
    bool used{false};
    std::size_t listener{0};
    TcpState state{TcpState::ReadQuery};
    int client_fd{-1};
    int up_fd{-1};
    std::size_t doh{kNoTransfer};  // DoH transfer in flight
    std::vector<std::uint8_t> query;   // 2-byte length + message; allocated on first use
    std::vector<std::uint8_t> answer;  // same
    std::size_t query_got{0};
    std::size_t answer_got{0};
    std::size_t answer_len{0};         // 2 + message length once known
    std::size_t answer_sent{0};
    std::uint32_t start{0};
    std::uint32_t tried{0};
    std::size_t current{0};
    bool ignore_dead{false};
    Clock::time_point attempt_start{};
    Clock::time_point deadline{};
};

// A batch of probe queries, handed to the forwarder thread.
struct ProbeJob {
    DnsBalancerSettings settings;
    std::vector<DnsProbeRequest> requests;
    std::vector<DnsProbeResult> results;
    std::vector<bool> filled;  // results that are final
    std::size_t next{0};      // the next request to start
    std::size_t finished{0};
    Clock::time_point deadline{};
    bool done{false};         // guarded by DnsBalancer::Impl::mutex
};

// One probe query in flight.
struct ProbeSlot {
    bool used{false};
    std::size_t request{0};
    int up_fd{-1};
    std::size_t doh{kNoTransfer};
    std::size_t endpoint{kNoTransfer};  // DoH: waits for the address of its host
    bool waiting{false};
    Clock::time_point start{};
    Clock::time_point deadline{};
};

} // namespace

const char* dns_upstream_outcome_name(DnsUpstreamOutcome outcome) {
    switch (outcome) {
        case DnsUpstreamOutcome::Ok: return "ok";
        case DnsUpstreamOutcome::Nxdomain: return "nxdomain";
        case DnsUpstreamOutcome::Servfail: return "servfail";
        case DnsUpstreamOutcome::Refused: return "refused";
        case DnsUpstreamOutcome::OtherRcode: return "other_rcode";
        case DnsUpstreamOutcome::Timeout: return "timeout";
        case DnsUpstreamOutcome::ConnectionRefused: return "connection_refused";
        case DnsUpstreamOutcome::Unreachable: return "unreachable";
        case DnsUpstreamOutcome::ConnectionClosed: return "connection_closed";
        case DnsUpstreamOutcome::TlsError: return "tls_error";
        case DnsUpstreamOutcome::HttpError: return "http_error";
        case DnsUpstreamOutcome::BadAnswer: return "bad_answer";
        case DnsUpstreamOutcome::NotResolved: return "not_resolved";
        case DnsUpstreamOutcome::NetworkError: return "network_error";
    }
    return "network_error";
}

struct DnsBalancer::Impl {
    explicit Impl(DnsBalancerOptions opts)
        : options(std::move(opts)), listen_ip(options.listen_ip) {}

    DnsBalancerOptions options;
    std::string listen_ip;  // loop thread: address of the open listeners

    // Control side.
    mutable std::mutex mutex;
    std::condition_variable applied_cv;
    std::vector<DnsBalancerGroup> pending;
    std::vector<DnsBalancerGroup> applied;
    DnsBalancerSettings pending_settings;
    DnsBalancerSettings applied_settings;
    std::vector<std::uint16_t> failed;
    std::uint64_t requested_generation{0};
    std::uint64_t applied_generation{0};
    std::thread thread;
    bool stopping{false};
    // stats(): the loop thread answers the request with the highest number.
    std::condition_variable stats_cv;
    std::uint64_t stats_requested{0};
    std::uint64_t stats_served{0};
    DnsBalancerStats last_stats;
    // probe(): the batch waiting for the loop thread.
    std::shared_ptr<ProbeJob> probe_job;
    std::condition_variable probe_cv;

    // Loop side: touched by the thread only (and by stop() after join).
    int epoll_fd{-1};
    int wake_fd{-1};
    std::array<Listener, kMaxListeners> listeners;
    std::vector<UdpSlot> udp_slots;
    std::vector<TcpSession> tcp_sessions;
    std::array<std::uint8_t, kUdpAnswerMax> udp_answer{};
    DnsBalancerSettings settings;  // the applied ones, loop side
    std::shared_ptr<ProbeJob> active_job;  // the batch being worked on
    std::vector<ProbeSlot> probe_slots;
    std::map<std::string, Streak> streaks;  // by upstream identity
    std::set<std::string> failing_reported;  // the failing identities the callback last heard of
#ifdef WITH_DOH
    std::unique_ptr<DohClient> doh;
    std::unique_ptr<DohBootstrap> bootstrap;
    std::map<std::string, std::size_t> doh_endpoints;           // url|mark|ips -> endpoint id
    std::set<std::size_t> doh_fixed;                            // endpoints with fixed addresses
    std::vector<DnsBalancerUpstream> bootstrap_servers;         // the ones the lookups go to
    std::map<std::string, std::string> bootstrap_failures;      // host -> what the servers said
    std::map<std::string, std::vector<std::string>> doh_hosts;  // host -> its looked up addresses
    std::map<std::string, Clock::time_point> bootstrap_asked;
#endif

    // --- lifecycle -------------------------------------------------------

    bool start_thread_locked() {
        if (thread.joinable()) {
            return true;
        }
        epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
        wake_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (epoll_fd < 0 || wake_fd < 0) {
            Logger::instance().warn("DNS forwarder: cannot create epoll/eventfd: {}",
                                    std::strerror(errno));
            close_fd(epoll_fd);
            close_fd(wake_fd);
            return false;
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = make_tag(Kind::Wake, 0);
        ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wake_fd, &ev);
        udp_slots.resize(kUdpSlots);
        tcp_sessions.resize(kTcpSessions);
        probe_slots.assign(kProbeSlots, ProbeSlot{});
        stopping = false;
        thread = std::thread([this] { loop(); });
        return true;
    }

    void wake() {
        const std::uint64_t one = 1;
        if (wake_fd >= 0) {
            [[maybe_unused]] const ssize_t n = ::write(wake_fd, &one, sizeof(one));
        }
    }

    void stop() {
        std::thread to_join;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable()) {
                return;
            }
            stopping = true;
            to_join = std::move(thread);
        }
        wake();
        to_join.join();
#ifdef WITH_DOH
        bootstrap.reset();
        doh.reset();
        doh_endpoints.clear();
        doh_fixed.clear();
        bootstrap_servers.clear();
        bootstrap_failures.clear();
        doh_hosts.clear();
        bootstrap_asked.clear();
#endif
        streaks.clear();
        failing_reported.clear();
        {
            std::lock_guard<std::mutex> lock(mutex);
            last_stats = {};
            applied.clear();
            applied_settings = {};
            applied_generation = requested_generation;
        }
        applied_cv.notify_all();
        close_fd(epoll_fd);
        close_fd(wake_fd);
    }

    // --- control thread ---------------------------------------------------

    std::vector<std::uint16_t> configure(const std::vector<DnsBalancerGroup>& groups,
                                         const DnsBalancerSettings& new_settings) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!thread.joinable()) {
            if (groups.empty()) {
                return {};
            }
            if (!start_thread_locked()) {
                std::vector<std::uint16_t> all;
                for (const auto& group : groups) all.push_back(group.port);
                return all;
            }
        }
        if (groups == applied && new_settings == applied_settings) {
            return failed;
        }
        pending = groups;
        pending_settings = new_settings;
        const std::uint64_t generation = ++requested_generation;
        wake();
        applied_cv.wait(lock, [&] { return applied_generation >= generation; });
        return failed;
    }

    std::vector<DnsProbeResult> probe(const std::vector<DnsProbeRequest>& requests,
                                      std::chrono::milliseconds deadline,
                                      const DnsBalancerSettings& probe_settings) {
        if (requests.empty()) {
            return {};
        }
        auto job = std::make_shared<ProbeJob>();
        job->settings = probe_settings;
        job->requests = requests;
        job->results.resize(requests.size());
        job->filled.assign(requests.size(), false);
        job->deadline = Clock::now() + deadline;
        std::unique_lock<std::mutex> lock(mutex);
        probe_cv.wait(lock, [&] { return probe_job == nullptr; });
        if (!thread.joinable()) {
            if (!start_thread_locked()) {
                for (auto& result : job->results) {
                    result.usable = false;
                    result.error = "cannot start the DNS client";
                }
                return std::move(job->results);
            }
        }
        probe_job = job;
        wake();
        probe_cv.wait(lock, [&] { return job->done; });
        return std::move(job->results);
    }

    DnsBalancerStats stats() {
        std::unique_lock<std::mutex> lock(mutex);
        if (!thread.joinable()) {
            return last_stats;
        }
        const std::uint64_t request = ++stats_requested;
        wake();
        stats_cv.wait_for(lock, std::chrono::milliseconds(500),
                          [&] { return stats_served >= request; });
        return last_stats;
    }

    // --- loop thread --------------------------------------------------------

    void loop() {
        std::array<epoll_event, 32> events{};
        for (;;) {
            const auto now = Clock::now();
            const int timeout = next_timeout_ms(now);
            const int n = ::epoll_wait(epoll_fd, events.data(), static_cast<int>(events.size()),
                                       timeout);
            if (n < 0 && errno != EINTR) {
                Logger::instance().warn("DNS forwarder: epoll_wait failed: {}",
                                        std::strerror(errno));
                break;
            }
            bool reconfigure = false;
            for (int i = 0; i < n; ++i) {
                const auto tag = events[static_cast<std::size_t>(i)].data.u64;
                const auto kind = static_cast<Kind>(tag >> 32);
                const auto index = static_cast<std::size_t>(tag & 0xFFFFFFFFu);
                const std::uint32_t flags = events[static_cast<std::size_t>(i)].events;
                switch (kind) {
                    case Kind::Wake: reconfigure = true; break;
                    case Kind::ListenUdp: on_listen_udp(index); break;
                    case Kind::ListenTcp: on_listen_tcp(index); break;
                    case Kind::UdpUp: on_udp_up(index); break;
                    case Kind::TcpClient: on_tcp_client(index, flags); break;
                    case Kind::TcpUp: on_tcp_up(index, flags); break;
                    case Kind::Doh: on_doh_socket(static_cast<int>(index), flags); break;
                    case Kind::ProbeUp: on_probe_up(index); break;
                }
            }
#ifdef WITH_DOH
            if (doh && doh->next_timeout_ms() == 0) {
                doh->handle_timeout();
            }
#endif
            expire(Clock::now());
            if (reconfigure) {
                std::uint64_t counter = 0;
                [[maybe_unused]] const ssize_t r = ::read(wake_fd, &counter, sizeof(counter));
                if (should_stop()) {
                    break;
                }
                apply_pending();
                apply_bootstrap_results();
                resume_waiting_probes();
                serve_stats();
                pump_probes();
            } else if (should_stop()) {
                break;
            }
        }
        for (std::size_t i = 0; i < kMaxListeners; ++i) {
            drop_listener(i);
        }
        abort_probes();
    }

    bool should_stop() {
        std::lock_guard<std::mutex> lock(mutex);
        return stopping;
    }

    int next_timeout_ms(Clock::time_point now) const {
        Clock::time_point earliest = Clock::time_point::max();
        for (const auto& slot : udp_slots) {
            if (slot.used) earliest = std::min(earliest, slot.deadline);
        }
        for (const auto& session : tcp_sessions) {
            if (session.used) earliest = std::min(earliest, session.deadline);
        }
        for (const auto& slot : probe_slots) {
            if (slot.used) earliest = std::min(earliest, slot.deadline);
        }
        int result = -1;
        if (earliest != Clock::time_point::max()) {
            result = earliest <= now
                         ? 0
                         : static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                earliest - now)
                                                .count() +
                                            1);
        }
#ifdef WITH_DOH
        if (doh) {
            const int curl_wait = doh->next_timeout_ms();
            if (curl_wait >= 0 && (result < 0 || curl_wait < result)) {
                result = curl_wait;
            }
        }
#endif
        return result;
    }

    void epoll_add(int fd, std::uint32_t events, Kind kind, std::size_t index) {
        epoll_event ev{};
        ev.events = events;
        ev.data.u64 = make_tag(kind, index);
        ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }

    void epoll_mod(int fd, std::uint32_t events, Kind kind, std::size_t index) {
        epoll_event ev{};
        ev.events = events;
        ev.data.u64 = make_tag(kind, index);
        ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }

    // --- configuration ------------------------------------------------------

    void apply_pending() {
        std::vector<DnsBalancerGroup> groups;
        DnsBalancerSettings new_settings;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (applied_generation >= requested_generation) {
                return;  // woken for something else
            }
            groups = pending;
            new_settings = pending_settings;
            generation = requested_generation;
        }
        std::vector<std::uint16_t> bind_failed;
        settings = new_settings;
        const std::string& wanted_ip =
            settings.listen_ip.empty() ? options.listen_ip : settings.listen_ip;
        if (wanted_ip != listen_ip) {
            for (std::size_t i = 0; i < kMaxListeners; ++i) {
                if (listeners[i].used) drop_listener(i);
            }
            listen_ip = wanted_ip;
        }
        prepare_doh();

        for (std::size_t i = 0; i < kMaxListeners; ++i) {
            if (listeners[i].used &&
                std::none_of(groups.begin(), groups.end(), [&](const DnsBalancerGroup& g) {
                    return g.port == listeners[i].port;
                })) {
                drop_listener(i);
            }
        }
        for (const auto& group : groups) {
            std::size_t slot = kMaxListeners;
            for (std::size_t i = 0; i < kMaxListeners; ++i) {
                if (listeners[i].used && listeners[i].port == group.port) {
                    slot = i;
                    break;
                }
            }
            if (slot == kMaxListeners) {
                for (std::size_t i = 0; i < kMaxListeners; ++i) {
                    if (!listeners[i].used) {
                        slot = i;
                        break;
                    }
                }
                if (slot == kMaxListeners || !open_listener(slot, group.port)) {
                    bind_failed.push_back(group.port);
                    continue;
                }
            }
            set_upstreams(listeners[slot], group);
        }
        forget_unused_streaks();
        {
            std::lock_guard<std::mutex> lock(mutex);
            applied = groups;
            applied_settings = new_settings;
            failed = std::move(bind_failed);
            applied_generation = generation;
        }
        applied_cv.notify_all();
        notify_health_if_changed();
    }

    void set_upstreams(Listener& listener, const DnsBalancerGroup& group) {
        std::vector<Upstream> fresh;
        fresh.reserve(group.upstreams.size());
        for (const auto& spec : group.upstreams) {
            Upstream upstream;
            upstream.spec = spec;
            upstream.identity = upstream_identity(spec);
            if (upstream.is_doh()) {
                if (!prepare_doh_upstream(upstream)) {
                    upstream.usable = false;
                }
            } else if (!make_addr(spec.ip, spec.port, upstream.addr, upstream.addr_len)) {
                Logger::instance().warn("DNS forwarder: ignoring upstream '{}'", spec.ip);
                continue;
            }
            for (const auto& old : listener.upstreams) {
                if (old.spec == upstream.spec) {
                    upstream.dead_until = old.dead_until;
                    upstream.counters = old.counters;
                }
            }
            fresh.push_back(std::move(upstream));
        }
        listener.upstreams = std::move(fresh);
    }

    // --- DoH ------------------------------------------------------------------

    void prepare_doh() {
#ifdef WITH_DOH
        if (!settings.doh_enabled) {
            return;
        }
        if (!doh) {
            DohClientOptions client_options;
            client_options.epoll_fd = epoll_fd;
            client_options.tag_kind = static_cast<std::uint32_t>(Kind::Doh);
            client_options.ca_path = settings.ca_path;
            client_options.ca_is_directory = settings.ca_is_directory;
            client_options.allow_plain_http = options.allow_plain_http;
            auto client = std::make_unique<DohClient>(client_options, &Impl::doh_done_thunk, this);
            if (!client->valid()) {
                Logger::instance().warn("DNS forwarder: cannot start the DoH client");
                settings.doh_enabled = false;
                return;
            }
            doh = std::move(client);
        }
        if (!bootstrap) {
            bootstrap = std::make_unique<DohBootstrap>([this] { wake(); });
        }
        // The forwarder must never look the DoH host up through itself.
        std::vector<DnsBalancerUpstream> servers;
        for (const auto& server : settings.bootstrap) {
            if (server.ip != listen_ip) servers.push_back(server);
        }
        if (servers != bootstrap_servers) {
            const bool known_hosts = !doh_endpoints.empty();
            bootstrap_servers = servers;
            bootstrap->set_servers(std::move(servers));
            // The addresses found so far stay in use until the new ones come.
            std::set<std::string> hosts;
            for (const auto& [key, endpoint] : doh_endpoints) {
                if (doh_fixed.count(endpoint) == 0) hosts.insert(doh->endpoint_host(endpoint));
            }
            if (known_hosts) {
                for (const auto& host : hosts) request_bootstrap(host, true);
            }
        }
#endif
    }

    // Sets up the DoH client endpoint of `upstream`. False: it cannot be used.
    bool prepare_doh_upstream([[maybe_unused]] Upstream& upstream) {
#ifdef WITH_DOH
        if (!settings.doh_enabled || !doh) {
            return false;
        }
        std::uint32_t mark = 0;
        if (!upstream.spec.detour.empty()) {
            const auto found = settings.detour_marks.find(upstream.spec.detour);
            if (found == settings.detour_marks.end() || found->second == 0) {
                Logger::instance().warn("DNS forwarder: DoH server {} cannot use outbound '{}', "
                                        "which has no routing mark; it is not used",
                                        upstream.spec.url, upstream.spec.detour);
                return false;
            }
            mark = found->second;
        }
        std::string key = upstream.spec.url + "|" + std::to_string(mark);
        for (const auto& ip : upstream.spec.ips) key += "|" + ip;
        auto known = doh_endpoints.find(key);
        if (known == doh_endpoints.end()) {
            const std::size_t endpoint = doh->add_endpoint(upstream.spec.url, mark);
            if (endpoint == DohClient::kNoEndpoint) {
                Logger::instance().warn("DNS forwarder: DoH URL '{}' is not usable",
                                        upstream.spec.url);
                return false;
            }
            known = doh_endpoints.emplace(key, endpoint).first;
            const std::string& host = doh->endpoint_host(endpoint);
            if (!upstream.spec.ips.empty()) {
                doh_fixed.insert(endpoint);
                doh->set_addresses(endpoint, upstream.spec.ips);
            } else if (is_ip_literal(host)) {
                doh->set_addresses(endpoint, {host});
            } else {
                const auto cached = doh_hosts.find(host);
                if (cached != doh_hosts.end()) {
                    doh->set_addresses(endpoint, cached->second);
                } else {
                    request_bootstrap(host, true);
                }
            }
        }
        upstream.doh_endpoint = known->second;
        return true;
#else
        return false;
#endif
    }

#ifdef WITH_DOH
    void request_bootstrap(const std::string& host, bool force) {
        if (!bootstrap) return;
        const auto now = Clock::now();
        const auto asked = bootstrap_asked.find(host);
        if (!force && asked != bootstrap_asked.end() && now - asked->second < kBootstrapRetry) {
            return;
        }
        bootstrap_asked[host] = now;
        bootstrap->request(host);
    }
#endif

    void apply_bootstrap_results() {
#ifdef WITH_DOH
        if (!bootstrap || !doh) return;
        for (auto& result : bootstrap->take_results()) {
            if (result.ips.empty()) {  // keep what was known
                bootstrap_failures[result.host] = result.detail;
                continue;
            }
            bootstrap_failures.erase(result.host);
            for (std::size_t i = 0; i < kMaxListeners; ++i) {
                for (auto& upstream : listeners[i].upstreams) {
                    if (upstream.doh_endpoint != kNoTransfer &&
                        doh_fixed.count(upstream.doh_endpoint) == 0 &&
                        doh->endpoint_host(upstream.doh_endpoint) == result.host) {
                        doh->set_addresses(upstream.doh_endpoint, result.ips);
                        upstream.dead_until = {};
                    }
                }
            }
            for (const auto& [key, endpoint] : doh_endpoints) {
                if (doh_fixed.count(endpoint) == 0 && doh->endpoint_host(endpoint) == result.host) {
                    doh->set_addresses(endpoint, result.ips);
                }
            }
            doh_hosts[result.host] = std::move(result.ips);
        }
#endif
    }

    void on_doh_socket([[maybe_unused]] int fd, [[maybe_unused]] std::uint32_t flags) {
#ifdef WITH_DOH
        if (doh) doh->handle_socket_event(fd, flags);
#endif
    }

#ifdef WITH_DOH
    static void doh_done_thunk(void* context, std::uint64_t user, const DohResult& result) {
        static_cast<Impl*>(context)->on_doh_done(user, result);
    }
#endif

    // Starts `query` on the DoH upstream; kNoTransfer when it cannot be sent.
    std::size_t doh_send([[maybe_unused]] const Upstream& upstream,
                         [[maybe_unused]] const std::uint8_t* query,
                         [[maybe_unused]] std::size_t len, [[maybe_unused]] std::uint64_t user) {
#ifdef WITH_DOH
        if (!doh) return kNoTransfer;
        return doh->start(upstream.doh_endpoint, query, len, options.doh_attempt_timeout, user);
#else
        return kNoTransfer;
#endif
    }

    void doh_cancel([[maybe_unused]] std::size_t& transfer) {
#ifdef WITH_DOH
        if (transfer != kNoTransfer && doh) doh->cancel(transfer);
#endif
        transfer = kNoTransfer;
    }

#ifdef WITH_DOH
    void on_doh_done(std::uint64_t user, const DohResult& result) {
        const auto kind = static_cast<Kind>(user >> 32);
        const auto index = static_cast<std::size_t>(user & 0xFFFFFFFFu);
        const auto now = Clock::now();
        if (kind == Kind::ProbeUp) {
            on_probe_doh_done(index, result, now);
        } else if (kind == Kind::UdpUp && index < udp_slots.size() && udp_slots[index].used) {
            UdpSlot& slot = udp_slots[index];
            slot.doh = kNoTransfer;
            Listener& listener = listeners[slot.listener];
            if (!doh_usable_answer(result, kUdpAnswerMax, slot.listener, slot.current,
                                   slot.attempt_start, now)) {
                udp_attempt(slot, now);
                return;
            }
            std::memcpy(udp_answer.data(), result.body, result.body_len);
            udp_answer[0] = slot.query[0];
            udp_answer[1] = slot.query[1];
            record(listener, slot.current, outcome_for_rcode(udp_answer.data()), slot.attempt_start,
                   now);
            if (is_servfail(udp_answer.data()) && slot.tried < listener.upstreams.size()) {
                udp_attempt(slot, now);
                return;
            }
            mark_alive(listener, slot.current);
            ++listener.stats.responses_upstream;
            [[maybe_unused]] const ssize_t sent =
                ::sendto(listener.udp_fd, udp_answer.data(), result.body_len, MSG_NOSIGNAL,
                         reinterpret_cast<const sockaddr*>(&slot.client), sizeof(slot.client));
            release_udp(slot);
        } else if (kind == Kind::TcpUp && index < tcp_sessions.size() &&
                   tcp_sessions[index].used) {
            TcpSession& session = tcp_sessions[index];
            session.doh = kNoTransfer;
            Listener& listener = listeners[session.listener];
            if (!doh_usable_answer(result, kTcpAnswerMax, session.listener, session.current,
                                   session.attempt_start, now)) {
                tcp_attempt(index, now);
                return;
            }
            std::memcpy(session.answer.data() + 2, result.body, result.body_len);
            session.answer[0] = static_cast<std::uint8_t>(result.body_len >> 8);
            session.answer[1] = static_cast<std::uint8_t>(result.body_len & 0xFF);
            session.answer[2] = session.query[2];
            session.answer[3] = session.query[3];
            session.answer_len = 2 + result.body_len;
            record(listener, session.current, outcome_for_rcode(session.answer.data() + 2),
                   session.attempt_start, now);
            if (is_servfail(session.answer.data() + 2) && session.tried < listener.upstreams.size()) {
                tcp_attempt(index, now);
                return;
            }
            mark_alive(listener, session.current);
            ++listener.stats.responses_upstream;
            tcp_begin_write(index);
        }
    }

    // False (and the upstream marked dead) when the DoH attempt gave no answer.
    bool doh_usable_answer(const DohResult& result, std::size_t limit, std::size_t listener_index,
                           std::size_t upstream_index, Clock::time_point started,
                           Clock::time_point now) {
        Listener& listener = listeners[listener_index];
        if (result.ok && result.body_len <= limit && (result.body[2] & 0x80) != 0) {
            return true;
        }
        record(listener, upstream_index, outcome_for_doh(result), started, now,
               result.ok ? "unusable answer" : result.error);
        if (upstream_index < listener.upstreams.size()) {
            const Upstream& upstream = listener.upstreams[upstream_index];
            Logger::instance().verbose("DNS forwarder: DoH {} failed: {}", upstream.spec.url,
                                       result.ok ? "unusable answer" : result.error);
            if (result.connection_problem && doh && upstream.doh_endpoint != kNoTransfer &&
                doh_fixed.count(upstream.doh_endpoint) == 0) {
                request_bootstrap(doh->endpoint_host(upstream.doh_endpoint), false);
            }
        }
        mark_dead(listener, upstream_index, now);
        return false;
    }

    static DnsUpstreamOutcome outcome_for_doh(const DohResult& result) {
        if (result.ok) {
            return DnsUpstreamOutcome::BadAnswer;
        }
        switch (result.failure) {
            case DohFailure::Timeout: return DnsUpstreamOutcome::Timeout;
            case DohFailure::ConnectionRefused: return DnsUpstreamOutcome::ConnectionRefused;
            case DohFailure::Unreachable: return DnsUpstreamOutcome::Unreachable;
            case DohFailure::ConnectionClosed: return DnsUpstreamOutcome::ConnectionClosed;
            case DohFailure::TlsError: return DnsUpstreamOutcome::TlsError;
            case DohFailure::HttpError: return DnsUpstreamOutcome::HttpError;
            case DohFailure::BadAnswer: return DnsUpstreamOutcome::BadAnswer;
            case DohFailure::NotResolved: return DnsUpstreamOutcome::NotResolved;
            case DohFailure::None:
            case DohFailure::Other: break;
        }
        return DnsUpstreamOutcome::NetworkError;
    }
#endif

    // Why the host of a DoH endpoint has no address, naming the servers asked.
    std::string not_resolved_message([[maybe_unused]] std::size_t endpoint) const {
#ifdef WITH_DOH
        if (doh && endpoint != kNoTransfer) {
            const std::string& host = doh->endpoint_host(endpoint);
            const auto failed = bootstrap_failures.find(host);
            if (failed != bootstrap_failures.end()) {
                if (bootstrap_servers.empty()) {
                    return "could not find the address of " + host + " via the system resolver";
                }
                return "could not find the address of " + host + " via bootstrap servers: " +
                       failed->second;
            }
        }
#endif
        return "the host name of the server is not resolved";
    }

    // A DoH query could not be started on upstream `index`: count it when the
    // reason is that the host of the URL has no address (not a busy client).
    void record_doh_not_sent([[maybe_unused]] Listener& listener, [[maybe_unused]] std::size_t index,
                             [[maybe_unused]] Clock::time_point now) {
#ifdef WITH_DOH
        if (index < listener.upstreams.size() && doh &&
            !doh->endpoint_ready(listener.upstreams[index].doh_endpoint)) {
            record(listener, index, DnsUpstreamOutcome::NotResolved, now, now,
                   not_resolved_message(listener.upstreams[index].doh_endpoint));
        }
#endif
    }

    bool open_listener(std::size_t index, std::uint16_t port) {
        sockaddr_in addr{};
        if (!make_listen_addr(listen_ip, port, addr)) {
            return false;
        }
        Listener& listener = listeners[index];
        listener = Listener{};
        listener.port = port;
        const int one = 1;
        listener.udp_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        listener.tcp_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        bool ok = listener.udp_fd >= 0 && listener.tcp_fd >= 0;
        if (ok) {
            ::setsockopt(listener.tcp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            ok = ::bind(listener.udp_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0 &&
                 ::bind(listener.tcp_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0 &&
                 ::listen(listener.tcp_fd, 16) == 0;
        }
        if (!ok) {
            Logger::instance().warn("DNS forwarder: cannot listen on {}:{}: {}", listen_ip,
                                    port, std::strerror(errno));
            close_fd(listener.udp_fd);
            close_fd(listener.tcp_fd);
            listener = Listener{};
            return false;
        }
        listener.used = true;
        epoll_add(listener.udp_fd, EPOLLIN, Kind::ListenUdp, index);
        epoll_add(listener.tcp_fd, EPOLLIN, Kind::ListenTcp, index);
        return true;
    }

    void drop_listener(std::size_t index) {
        Listener& listener = listeners[index];
        if (!listener.used) {
            return;
        }
        for (auto& slot : udp_slots) {
            if (slot.used && slot.listener == index) release_udp(slot);
        }
        for (std::size_t i = 0; i < tcp_sessions.size(); ++i) {
            if (tcp_sessions[i].used && tcp_sessions[i].listener == index) release_tcp(i);
        }
        close_fd(listener.udp_fd);
        close_fd(listener.tcp_fd);
        listener = Listener{};
    }

    // --- upstream choice ------------------------------------------------------

    // Next upstream to try for a query that has tried `tried` of them; false
    // when none is left. `start` is the round-robin position of the query.
    bool pick(Listener& listener, std::uint32_t start, std::uint32_t& tried, bool ignore_dead,
              Clock::time_point now, std::size_t& chosen) {
        const std::size_t count = listener.upstreams.size();
        while (tried < count) {
            const std::size_t index = (start + tried) % count;
            ++tried;
            if (!listener.upstreams[index].usable) {
                continue;
            }
            if (ignore_dead || listener.upstreams[index].dead_until <= now) {
                chosen = index;
                return true;
            }
        }
        return false;
    }

    static bool all_dead(const Listener& listener, Clock::time_point now) {
        return std::all_of(listener.upstreams.begin(), listener.upstreams.end(),
                           [&](const Upstream& u) { return !u.usable || u.dead_until > now; });
    }

    void mark_dead(Listener& listener, std::size_t index, Clock::time_point now) {
        if (index < listener.upstreams.size()) {
            listener.upstreams[index].dead_until = now + options.dead_for;
        }
    }

    void mark_alive(Listener& listener, std::size_t index) {
        if (index < listener.upstreams.size()) {
            listener.upstreams[index].dead_until = {};
        }
    }

    // --- statistics -----------------------------------------------------------

    // One attempt to get an answer from upstream `index` ended with `outcome`.
    void record(Listener& listener, std::size_t index, DnsUpstreamOutcome outcome,
                Clock::time_point started, Clock::time_point now, std::string_view error = {}) {
        if (index >= listener.upstreams.size()) {
            return;
        }
        Upstream& upstream = listener.upstreams[index];
        UpstreamCounters& counters = upstream.counters;
        ++counters.results[static_cast<std::size_t>(outcome)];
        if (dns_upstream_outcome_answered(outcome)) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(now - started);
            const auto micros = static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed.count(), 0));
            counters.latency_sum_us += micros;
            std::size_t bucket = 0;
            while (bucket < kDnsLatencyBoundsMs.size() &&
                   micros > std::uint64_t{kDnsLatencyBoundsMs[bucket]} * 1000U) {
                ++bucket;
            }
            ++counters.latency_buckets[bucket];
        }
        Streak& streak = streaks[upstream.identity];
        const bool was_failing = streak.failing();
        if (dns_upstream_outcome_failed(outcome)) {
            ++streak.count;
            streak.last = outcome;
            streak.error.assign(error.empty() ? std::string_view(dns_upstream_outcome_name(outcome))
                                              : error);
        } else {
            streak.count = 0;
        }
        if (streak.failing() != was_failing) {
            notify_health_if_changed();
        }
    }

    // The identities that keep failing: some group has them in use.
    std::set<std::string> failing_identities() const {
        std::set<std::string> failing;
        for (const auto& [identity, streak] : streaks) {
            if (streak.failing()) failing.insert(identity);
        }
        return failing;
    }

    void notify_health_if_changed() {
        auto failing = failing_identities();
        if (failing == failing_reported) {
            return;
        }
        failing_reported = std::move(failing);
        if (options.on_health_change) {
            options.on_health_change();
        }
    }

    // Drops the streaks of upstreams that are gone or can no longer be tried.
    void forget_unused_streaks() {
        std::set<std::string> in_use;
        for (const auto& listener : listeners) {
            if (!listener.used) continue;
            for (const auto& upstream : listener.upstreams) {
                if (upstream.usable) in_use.insert(upstream.identity);
            }
        }
        for (auto it = streaks.begin(); it != streaks.end();) {
            it = in_use.count(it->first) == 0 ? streaks.erase(it) : std::next(it);
        }
    }

    // Answers the stats() requests made so far.
    void serve_stats() {
        std::uint64_t target = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stats_served >= stats_requested) {
                return;
            }
            target = stats_requested;
        }
        DnsBalancerStats snapshot = build_stats(Clock::now());
        {
            std::lock_guard<std::mutex> lock(mutex);
            last_stats = std::move(snapshot);
            stats_served = target;
        }
        stats_cv.notify_all();
    }

    DnsBalancerStats build_stats(Clock::time_point now) const {
        DnsBalancerStats snapshot;
        for (const auto& listener : listeners) {
            if (!listener.used) continue;
            DnsListenerStats item = listener.stats;
            item.port = listener.port;
            for (const auto& upstream : listener.upstreams) {
                DnsUpstreamStats stat;
                stat.upstream = upstream.spec;
                stat.usable = upstream.usable;
                stat.up = upstream.usable && upstream.dead_until <= now;
                stat.results = upstream.counters.results;
                stat.latency_buckets = upstream.counters.latency_buckets;
                stat.latency_sum_us = upstream.counters.latency_sum_us;
                const auto streak = streaks.find(upstream.identity);
                if (streak != streaks.end()) {
                    stat.consecutive_failures = streak->second.count;
                    stat.last_failure = streak->second.last;
                    stat.last_error = streak->second.error;
                }
                item.upstreams.push_back(std::move(stat));
            }
            snapshot.listeners.push_back(std::move(item));
        }
        for (const auto& slot : udp_slots) {
            if (slot.used) ++snapshot.inflight_udp;
        }
        for (const auto& session : tcp_sessions) {
            if (session.used) ++snapshot.inflight_tcp;
        }
        return snapshot;
    }

    // --- UDP --------------------------------------------------------------------

    void release_udp(UdpSlot& slot) {
        close_fd(slot.up_fd);
        doh_cancel(slot.doh);
        slot.used = false;
    }

    void on_listen_udp(std::size_t index) {
        Listener& listener = listeners[index];
        if (!listener.used) return;
        std::array<std::uint8_t, kUdpQueryMax + 1> buf{};
        for (int i = 0; i < kUdpPerWake; ++i) {
            sockaddr_in client{};
            socklen_t client_len = sizeof(client);
            const ssize_t n = ::recvfrom(listener.udp_fd, buf.data(), buf.size(), 0,
                                         reinterpret_cast<sockaddr*>(&client), &client_len);
            if (n < 0) {
                return;  // EAGAIN or a transient error
            }
            const auto len = static_cast<std::size_t>(n);
            ++listener.stats.queries_udp;
            if (len < kDnsHeaderLen || len > kUdpQueryMax || (buf[2] & 0x80) != 0) {
                ++listener.stats.dropped_malformed_udp;
                continue;  // not a query we can forward
            }
            UdpSlot* slot = nullptr;
            for (auto& candidate : udp_slots) {
                if (!candidate.used) {
                    slot = &candidate;
                    break;
                }
            }
            if (slot == nullptr) {
                ++listener.stats.dropped_overload_udp;
                continue;  // overloaded: the client retries
            }
            slot->used = true;
            slot->listener = index;
            slot->client = client;
            slot->len = static_cast<std::uint16_t>(len);
            std::memcpy(slot->query.data(), buf.data(), len);
            slot->start = listener.next++;
            slot->tried = 0;
            const auto now = Clock::now();
            slot->ignore_dead = all_dead(listener, now);
            udp_attempt(*slot, now);
        }
    }

    // Sends the query to the next upstream; answers SERVFAIL when none is left.
    void udp_attempt(UdpSlot& slot, Clock::time_point now) {
        Listener& listener = listeners[slot.listener];
        close_fd(slot.up_fd);
        doh_cancel(slot.doh);
        const auto slot_index = static_cast<std::size_t>(&slot - udp_slots.data());
        std::size_t chosen = 0;
        while (pick(listener, slot.start, slot.tried, slot.ignore_dead, now, chosen)) {
            const Upstream& upstream = listener.upstreams[chosen];
            if (upstream.is_doh()) {
                const std::size_t transfer = doh_send(upstream, slot.query.data(), slot.len,
                                                      make_tag(Kind::UdpUp, slot_index));
                if (transfer == kNoTransfer) {
                    record_doh_not_sent(listener, chosen, now);
                    continue;  // its host is not resolved yet, or no transfer is free
                }
                slot.doh = transfer;
                slot.current = chosen;
                slot.attempt_start = now;
                slot.deadline = now + options.doh_attempt_timeout;
                return;
            }
            const int fd = ::socket(upstream.addr.ss_family,
                                    SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                break;
            }
            const sockaddr_storage& target = upstream.addr;
            if (::connect(fd, reinterpret_cast<const sockaddr*>(&target), upstream.addr_len) != 0 ||
                ::send(fd, slot.query.data(), slot.len, MSG_NOSIGNAL) != slot.len) {
                const int error = errno;
                ::close(fd);
                mark_dead(listener, chosen, now);
                record(listener, chosen, outcome_for_errno(error), now, now, std::strerror(error));
                continue;
            }
            slot.up_fd = fd;
            slot.current = chosen;
            slot.attempt_start = now;
            slot.deadline = now + options.udp_attempt_timeout;
            epoll_add(fd, EPOLLIN, Kind::UdpUp, static_cast<std::size_t>(&slot - udp_slots.data()));
            return;
        }
        udp_servfail(slot);
    }

    void udp_servfail(UdpSlot& slot) {
        Listener& listener = listeners[slot.listener];
        const std::size_t len = build_servfail(slot.query.data(), slot.len, udp_answer.data());
        if (len != 0) {
            ++listener.stats.responses_servfail;
            [[maybe_unused]] const ssize_t n =
                ::sendto(listener.udp_fd, udp_answer.data(), len, MSG_NOSIGNAL,
                         reinterpret_cast<const sockaddr*>(&slot.client), sizeof(slot.client));
        }
        release_udp(slot);
    }

    void on_udp_up(std::size_t index) {
        if (index >= udp_slots.size() || !udp_slots[index].used) return;
        UdpSlot& slot = udp_slots[index];
        Listener& listener = listeners[slot.listener];
        const auto now = Clock::now();
        const ssize_t n = ::recv(slot.up_fd, udp_answer.data(), udp_answer.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            const int error = errno;
            mark_dead(listener, slot.current, now);  // e.g. connection refused
            record(listener, slot.current, outcome_for_errno(error), slot.attempt_start, now,
                   std::strerror(error));
            udp_attempt(slot, now);
            return;
        }
        const auto len = static_cast<std::size_t>(n);
        if (len < kDnsHeaderLen || udp_answer[0] != slot.query[0] || udp_answer[1] != slot.query[1] ||
            (udp_answer[2] & 0x80) == 0) {
            return;  // not the answer to this query; keep waiting
        }
        record(listener, slot.current, outcome_for_rcode(udp_answer.data()), slot.attempt_start, now);
        if (is_servfail(udp_answer.data()) && slot.tried < listener.upstreams.size()) {
            udp_attempt(slot, now);
            return;
        }
        mark_alive(listener, slot.current);
        ++listener.stats.responses_upstream;
        [[maybe_unused]] const ssize_t sent =
            ::sendto(listener.udp_fd, udp_answer.data(), len, MSG_NOSIGNAL,
                     reinterpret_cast<const sockaddr*>(&slot.client), sizeof(slot.client));
        release_udp(slot);
    }

    // --- TCP ----------------------------------------------------------------------

    void release_tcp(std::size_t index) {
        TcpSession& session = tcp_sessions[index];
        close_fd(session.client_fd);
        close_fd(session.up_fd);
        doh_cancel(session.doh);
        session.used = false;
    }

    void on_listen_tcp(std::size_t index) {
        Listener& listener = listeners[index];
        if (!listener.used) return;
        for (int i = 0; i < kAcceptPerWake; ++i) {
            const int fd = ::accept4(listener.tcp_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
                return;
            }
            std::size_t found = tcp_sessions.size();
            for (std::size_t s = 0; s < tcp_sessions.size(); ++s) {
                if (!tcp_sessions[s].used) {
                    found = s;
                    break;
                }
            }
            if (found == tcp_sessions.size()) {
                ++listener.stats.queries_tcp;
                ++listener.stats.dropped_overload_tcp;
                ::close(fd);  // overloaded: the client retries
                continue;
            }
            TcpSession& session = tcp_sessions[found];
            if (session.query.empty()) {
                session.query.resize(2 + kTcpQueryMax);
                session.answer.resize(2 + kTcpAnswerMax);
            }
            session.used = true;
            session.listener = index;
            session.client_fd = fd;
            session.up_fd = -1;
            session.doh = kNoTransfer;
            reset_for_next_query(session, Clock::now());
            epoll_add(fd, EPOLLIN | EPOLLRDHUP, Kind::TcpClient, found);
        }
    }

    void reset_for_next_query(TcpSession& session, Clock::time_point now) {
        session.state = TcpState::ReadQuery;
        session.query_got = 0;
        session.answer_got = 0;
        session.answer_len = 0;
        session.answer_sent = 0;
        session.deadline = now + options.tcp_idle;
    }

    void on_tcp_client(std::size_t index, std::uint32_t flags) {
        if (index >= tcp_sessions.size() || !tcp_sessions[index].used) return;
        TcpSession& session = tcp_sessions[index];
        if ((flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) != 0 &&
            session.state != TcpState::ReadQuery) {
            release_tcp(index);
            return;
        }
        if (session.state == TcpState::WriteAnswer) {
            tcp_write_answer(index);
            return;
        }
        if (session.state != TcpState::ReadQuery) {
            return;
        }
        for (;;) {
            const std::size_t want = session.query_got < 2
                                         ? 2 - session.query_got
                                         : 2 + read_be16(session.query.data()) - session.query_got;
            const ssize_t n = ::read(session.client_fd, session.query.data() + session.query_got, want);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                release_tcp(index);
                return;
            }
            if (n < 0) {
                return;
            }
            session.query_got += static_cast<std::size_t>(n);
            if (session.query_got < 2) {
                continue;
            }
            const std::size_t message = read_be16(session.query.data());
            if (message < kDnsHeaderLen || message > kTcpQueryMax) {
                ++listeners[session.listener].stats.queries_tcp;
                ++listeners[session.listener].stats.dropped_malformed_tcp;
                release_tcp(index);
                return;
            }
            if (session.query_got == 2 + message) {
                break;
            }
        }
        Listener& listener = listeners[session.listener];
        ++listener.stats.queries_tcp;
        const auto now = Clock::now();
        session.start = listener.next++;
        session.tried = 0;
        session.ignore_dead = all_dead(listener, now);
        epoll_mod(session.client_fd, EPOLLRDHUP, Kind::TcpClient, index);
        tcp_attempt(index, now);
    }

    void tcp_servfail(std::size_t index) {
        TcpSession& session = tcp_sessions[index];
        const std::size_t message = read_be16(session.query.data());
        const std::size_t len = build_servfail(session.query.data() + 2, message, session.answer.data() + 2);
        if (len == 0) {
            release_tcp(index);
            return;
        }
        session.answer[0] = static_cast<std::uint8_t>(len >> 8);
        session.answer[1] = static_cast<std::uint8_t>(len & 0xFF);
        session.answer_len = 2 + len;
        ++listeners[session.listener].stats.responses_servfail;
        tcp_begin_write(index);
    }

    void tcp_attempt(std::size_t index, Clock::time_point now) {
        TcpSession& session = tcp_sessions[index];
        Listener& listener = listeners[session.listener];
        close_fd(session.up_fd);
        doh_cancel(session.doh);
        std::size_t chosen = 0;
        while (pick(listener, session.start, session.tried, session.ignore_dead, now, chosen)) {
            const Upstream& upstream = listener.upstreams[chosen];
            if (upstream.is_doh()) {
                const std::size_t message = read_be16(session.query.data());
                const std::size_t transfer = doh_send(upstream, session.query.data() + 2, message,
                                                      make_tag(Kind::TcpUp, index));
                if (transfer == kNoTransfer) {
                    record_doh_not_sent(listener, chosen, now);
                    continue;  // its host is not resolved yet, or no transfer is free
                }
                session.doh = transfer;
                session.current = chosen;
                session.attempt_start = now;
                session.state = TcpState::ReadAnswer;
                session.answer_got = 0;
                session.answer_len = 0;
                session.deadline = now + options.doh_attempt_timeout;
                return;
            }
            const int fd = ::socket(upstream.addr.ss_family,
                                    SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                break;
            }
            const sockaddr_storage& target = upstream.addr;
            const int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&target), upstream.addr_len);
            if (rc != 0 && errno != EINPROGRESS) {
                const int error = errno;
                ::close(fd);
                mark_dead(listener, chosen, now);
                record(listener, chosen, outcome_for_errno(error), now, now, std::strerror(error));
                continue;
            }
            session.up_fd = fd;
            session.current = chosen;
            session.attempt_start = now;
            session.state = TcpState::Connect;
            session.answer_got = 0;
            session.answer_len = 0;
            session.deadline = now + options.tcp_attempt_timeout;
            epoll_add(fd, EPOLLOUT, Kind::TcpUp, index);
            return;
        }
        tcp_servfail(index);
    }

    void tcp_fail_attempt(std::size_t index, Clock::time_point now, DnsUpstreamOutcome outcome,
                          std::string_view error = {}) {
        TcpSession& session = tcp_sessions[index];
        Listener& listener = listeners[session.listener];
        mark_dead(listener, session.current, now);
        record(listener, session.current, outcome, session.attempt_start, now, error);
        tcp_attempt(index, now);
    }

    void on_tcp_up(std::size_t index, std::uint32_t flags) {
        if (index >= tcp_sessions.size() || !tcp_sessions[index].used) return;
        TcpSession& session = tcp_sessions[index];
        if (session.up_fd < 0) return;
        const auto now = Clock::now();
        if (session.state == TcpState::Connect) {
            int error = 0;
            socklen_t error_len = sizeof(error);
            ::getsockopt(session.up_fd, SOL_SOCKET, SO_ERROR, &error, &error_len);
            if (error != 0 || (flags & (EPOLLERR | EPOLLHUP)) != 0) {
                tcp_fail_attempt(index, now,
                                 error != 0 ? outcome_for_errno(error) : DnsUpstreamOutcome::NetworkError,
                                 error != 0 ? std::strerror(error) : "connection failed");
                return;
            }
            const std::size_t total = 2 + read_be16(session.query.data());
            if (::send(session.up_fd, session.query.data(), total, MSG_NOSIGNAL) !=
                static_cast<ssize_t>(total)) {
                const int send_error = errno;
                tcp_fail_attempt(index, now, outcome_for_errno(send_error), std::strerror(send_error));
                return;
            }
            session.state = TcpState::ReadAnswer;
            session.answer_got = 0;
            session.answer_len = 0;
            epoll_mod(session.up_fd, EPOLLIN, Kind::TcpUp, index);
            return;
        }
        if (session.state != TcpState::ReadAnswer) return;
        for (;;) {
            const std::size_t want = session.answer_got < 2
                                         ? 2 - session.answer_got
                                         : session.answer_len - session.answer_got;
            const ssize_t n = ::read(session.up_fd, session.answer.data() + session.answer_got, want);
            if (n == 0) {
                tcp_fail_attempt(index, now, DnsUpstreamOutcome::ConnectionClosed,
                                 "connection closed before the answer");
                return;
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                const int read_error = errno;
                tcp_fail_attempt(index, now, outcome_for_errno(read_error), std::strerror(read_error));
                return;
            }
            if (n < 0) {
                return;
            }
            session.answer_got += static_cast<std::size_t>(n);
            if (session.answer_got >= 2 && session.answer_len == 0) {
                const std::size_t message = read_be16(session.answer.data());
                if (message < kDnsHeaderLen || message > kTcpAnswerMax) {
                    tcp_fail_attempt(index, now, DnsUpstreamOutcome::BadAnswer,
                                     "answer of an unexpected size");
                    return;
                }
                session.answer_len = 2 + message;
            }
            if (session.answer_len != 0 && session.answer_got == session.answer_len) {
                break;
            }
        }
        Listener& listener = listeners[session.listener];
        if (session.answer[2] != session.query[2] || session.answer[3] != session.query[3]) {
            tcp_fail_attempt(index, now, DnsUpstreamOutcome::BadAnswer, "answer to another query");
            return;
        }
        record(listener, session.current, outcome_for_rcode(session.answer.data() + 2),
               session.attempt_start, now);
        if (is_servfail(session.answer.data() + 2) && session.tried < listener.upstreams.size()) {
            tcp_attempt(index, now);
            return;
        }
        mark_alive(listener, session.current);
        ++listener.stats.responses_upstream;
        close_fd(session.up_fd);
        tcp_begin_write(index);
    }

    void tcp_begin_write(std::size_t index) {
        TcpSession& session = tcp_sessions[index];
        close_fd(session.up_fd);
        session.state = TcpState::WriteAnswer;
        session.answer_sent = 0;
        session.deadline = Clock::now() + options.tcp_attempt_timeout;
        tcp_write_answer(index);
    }

    void tcp_write_answer(std::size_t index) {
        TcpSession& session = tcp_sessions[index];
        while (session.answer_sent < session.answer_len) {
            const ssize_t n = ::send(session.client_fd, session.answer.data() + session.answer_sent,
                                     session.answer_len - session.answer_sent, MSG_NOSIGNAL);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                epoll_mod(session.client_fd, EPOLLOUT | EPOLLRDHUP, Kind::TcpClient, index);
                return;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                release_tcp(index);
                return;
            }
            session.answer_sent += static_cast<std::size_t>(n);
        }
        reset_for_next_query(session, Clock::now());
        epoll_mod(session.client_fd, EPOLLIN | EPOLLRDHUP, Kind::TcpClient, index);
    }

    // --- probes -----------------------------------------------------------------

    // Takes over the batch handed in by probe() and starts what fits.
    void pump_probes() {
        if (!active_job) {
            std::lock_guard<std::mutex> lock(mutex);
            if (probe_job && !probe_job->done) {
                active_job = probe_job;
            }
        }
        if (active_job && active_job->next == 0) {
            merge_probe_settings(active_job->settings);
        }
        while (active_job && active_job->next < active_job->requests.size()) {
            std::size_t slot = probe_slots.size();
            for (std::size_t i = 0; i < probe_slots.size(); ++i) {
                if (!probe_slots[i].used) {
                    slot = i;
                    break;
                }
            }
            if (slot == probe_slots.size()) {
                return;  // the rest starts as slots free up
            }
            start_probe(slot, active_job->next++);
        }
    }

    // The groups' settings stay; what DoH needs and they lack is added.
    void merge_probe_settings(const DnsBalancerSettings& wanted) {
        if (wanted.doh_enabled && !settings.doh_enabled) {
            settings.doh_enabled = true;
            settings.ca_path = wanted.ca_path;
            settings.ca_is_directory = wanted.ca_is_directory;
        }
        for (const auto& [tag, mark] : wanted.detour_marks) {
            settings.detour_marks.emplace(tag, mark);
        }
        if (settings.bootstrap.empty()) {
            settings.bootstrap = wanted.bootstrap;
        }
        prepare_doh();
    }

    // Sends request `request` of the batch through slot `index`.
    void start_probe(std::size_t index, std::size_t request) {
        ProbeSlot& slot = probe_slots[index];
        const DnsProbeRequest& wanted = active_job->requests[request];
        const auto now = Clock::now();
        slot = ProbeSlot{};
        slot.used = true;
        slot.request = request;
        slot.start = now;
        if (wanted.query.size() < kDnsHeaderLen) {
            finish_probe(index, DnsUpstreamOutcome::BadAnswer, "the query is not a DNS message",
                         nullptr, 0, now);
            return;
        }
        if (wanted.upstream.kind == DnsBalancerUpstreamKind::Doh) {
            start_probe_doh(index, wanted, now);
            return;
        }
        sockaddr_storage addr{};
        socklen_t addr_len = 0;
        if (!make_addr(wanted.upstream.ip, wanted.upstream.port, addr, addr_len)) {
            finish_probe(index, DnsUpstreamOutcome::NetworkError, "invalid server address", nullptr,
                         0, now);
            return;
        }
        const int fd = ::socket(addr.ss_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            const int error = errno;
            finish_probe(index, outcome_for_errno(error), std::strerror(error), nullptr, 0, now);
            return;
        }
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0 ||
            ::send(fd, wanted.query.data(), wanted.query.size(), MSG_NOSIGNAL) !=
                static_cast<ssize_t>(wanted.query.size())) {
            const int error = errno;
            ::close(fd);
            finish_probe(index, outcome_for_errno(error), std::strerror(error), nullptr, 0, now);
            return;
        }
        slot.up_fd = fd;
        slot.deadline = std::min(active_job->deadline, now + options.udp_attempt_timeout);
        epoll_add(fd, EPOLLIN, Kind::ProbeUp, index);
    }

    void start_probe_doh([[maybe_unused]] std::size_t index,
                         [[maybe_unused]] const DnsProbeRequest& wanted,
                         [[maybe_unused]] Clock::time_point now) {
#ifdef WITH_DOH
        ProbeSlot& slot = probe_slots[index];
        const auto& spec = wanted.upstream;
        if (!settings.doh_enabled || !doh) {
            finish_probe(index, DnsUpstreamOutcome::NetworkError,
                         "DNS-over-HTTPS is not available", nullptr, 0, now, false);
            return;
        }
        if (!spec.detour.empty()) {
            const auto mark = settings.detour_marks.find(spec.detour);
            if (mark == settings.detour_marks.end() || mark->second == 0) {
                finish_probe(index, DnsUpstreamOutcome::NetworkError,
                             "the outbound has no routing mark", nullptr, 0, now, false);
                return;
            }
        }
        Upstream upstream;
        upstream.spec = spec;
        if (!prepare_doh_upstream(upstream)) {
            finish_probe(index, DnsUpstreamOutcome::NetworkError, "invalid DNS-over-HTTPS URL",
                         nullptr, 0, now, false);
            return;
        }
        slot.endpoint = upstream.doh_endpoint;
        slot.deadline = active_job->deadline;
        if (!doh->endpoint_ready(slot.endpoint)) {
            slot.waiting = true;
            if (doh_fixed.count(slot.endpoint) == 0) {
                request_bootstrap(doh->endpoint_host(slot.endpoint), true);
            }
            return;
        }
        send_probe_doh(index, now);
#else
        finish_probe(index, DnsUpstreamOutcome::NetworkError,
                     "this build has no DNS-over-HTTPS support", nullptr, 0, now, false);
#endif
    }

#ifdef WITH_DOH
    void send_probe_doh(std::size_t index, Clock::time_point now) {
        ProbeSlot& slot = probe_slots[index];
        const DnsProbeRequest& wanted = active_job->requests[slot.request];
        const auto timeout = std::max<Clock::duration>(
            std::min<Clock::duration>(options.doh_attempt_timeout, active_job->deadline - now),
            std::chrono::milliseconds(1));
        const std::size_t transfer =
            doh->start(slot.endpoint, wanted.query.data(), wanted.query.size(),
                       std::chrono::duration_cast<std::chrono::milliseconds>(timeout),
                       make_tag(Kind::ProbeUp, index));
        if (transfer == kNoTransfer) {
            finish_probe(index, DnsUpstreamOutcome::NetworkError, "cannot start the request",
                         nullptr, 0, now);
            return;
        }
        slot.doh = transfer;
        slot.waiting = false;
        slot.start = now;
        slot.deadline = std::min(active_job->deadline, now + options.doh_attempt_timeout);
    }
#endif

    // The host of a DoH probe got an address.
    void resume_waiting_probes() {
#ifdef WITH_DOH
        if (!doh || !active_job) return;
        const auto now = Clock::now();
        for (std::size_t i = 0; i < probe_slots.size(); ++i) {
            if (probe_slots[i].used && probe_slots[i].waiting &&
                doh->endpoint_ready(probe_slots[i].endpoint)) {
                send_probe_doh(i, now);
            }
        }
#endif
    }

    void on_probe_up(std::size_t index) {
        if (index >= probe_slots.size() || !probe_slots[index].used || probe_slots[index].up_fd < 0) {
            return;
        }
        ProbeSlot& slot = probe_slots[index];
        const DnsProbeRequest& wanted = active_job->requests[slot.request];
        const auto now = Clock::now();
        const ssize_t n = ::recv(slot.up_fd, udp_answer.data(), udp_answer.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            const int error = errno;
            finish_probe(index, outcome_for_errno(error), std::strerror(error), nullptr, 0, now);
            return;
        }
        const auto len = static_cast<std::size_t>(n);
        if (len < kDnsHeaderLen || udp_answer[0] != wanted.query[0] || udp_answer[1] != wanted.query[1] ||
            (udp_answer[2] & 0x80) == 0) {
            return;  // not the answer to this query; keep waiting
        }
        finish_probe(index, outcome_for_rcode(udp_answer.data()), {}, udp_answer.data(), len, now);
    }

#ifdef WITH_DOH
    void on_probe_doh_done(std::size_t index, const DohResult& result, Clock::time_point now) {
        if (index >= probe_slots.size() || !probe_slots[index].used) return;
        probe_slots[index].doh = kNoTransfer;
        const DnsProbeRequest& wanted = active_job->requests[probe_slots[index].request];
        if (result.ok && (result.body[2] & 0x80) != 0) {
            std::vector<std::uint8_t> answer(result.body, result.body + result.body_len);
            answer[0] = wanted.query[0];
            answer[1] = wanted.query[1];
            finish_probe(index, outcome_for_rcode(answer.data()), {}, answer.data(), answer.size(), now);
            return;
        }
        finish_probe(index, outcome_for_doh(result), result.ok ? "unusable answer" : result.error,
                     nullptr, 0, now);
    }
#endif

    // Ends the probe of slot `index` with its result and starts the next.
    void finish_probe(std::size_t index, DnsUpstreamOutcome outcome, std::string_view error,
                      const std::uint8_t* answer, std::size_t answer_len, Clock::time_point now,
                      bool usable = true) {
        ProbeSlot& slot = probe_slots[index];
        DnsProbeResult& result = active_job->results[slot.request];
        active_job->filled[slot.request] = true;
        result.outcome = outcome;
        result.usable = usable;
        result.error.assign(error);
        if (answer != nullptr) {
            result.answer.assign(answer, answer + answer_len);
        }
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(now - slot.start);
        close_fd(slot.up_fd);
        doh_cancel(slot.doh);
        slot.used = false;
        slot.waiting = false;
        if (++active_job->finished == active_job->requests.size()) {
            complete_probe_job();
        } else {
            pump_probes();
        }
    }

    void complete_probe_job() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            active_job->done = true;
            if (probe_job == active_job) probe_job.reset();
        }
        active_job.reset();
        probe_cv.notify_all();
    }

    // The thread ends: whatever was not answered is not going to be.
    void abort_probes() {
        std::shared_ptr<ProbeJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            job = active_job ? active_job : probe_job;
        }
        for (auto& slot : probe_slots) {
            close_fd(slot.up_fd);
            doh_cancel(slot.doh);
            slot.used = false;
        }
        if (!job) return;
        active_job = job;
        for (std::size_t i = 0; i < job->results.size(); ++i) {
            if (!job->filled[i]) {
                job->results[i].usable = false;
                job->results[i].error = "the DNS client stopped";
            }
        }
        complete_probe_job();
    }

    // --- deadlines ------------------------------------------------------------------

    void expire(Clock::time_point now) {
        for (std::size_t i = 0; i < probe_slots.size(); ++i) {
            if (probe_slots[i].used && probe_slots[i].deadline <= now) {
                finish_probe(i, probe_slots[i].waiting ? DnsUpstreamOutcome::NotResolved
                                                       : DnsUpstreamOutcome::Timeout,
                             probe_slots[i].waiting ? not_resolved_message(probe_slots[i].endpoint)
                                                    : std::string("no answer in time"),
                             nullptr, 0, now);
            }
        }

        for (auto& slot : udp_slots) {
            if (slot.used && slot.deadline <= now) {
                Listener& listener = listeners[slot.listener];
                mark_dead(listener, slot.current, now);
                record(listener, slot.current, DnsUpstreamOutcome::Timeout, slot.attempt_start, now,
                       "no answer in time");
                udp_attempt(slot, now);
            }
        }
        for (std::size_t i = 0; i < tcp_sessions.size(); ++i) {
            TcpSession& session = tcp_sessions[i];
            if (!session.used || session.deadline > now) continue;
            if (session.state == TcpState::Connect || session.state == TcpState::ReadAnswer) {
                tcp_fail_attempt(i, now, DnsUpstreamOutcome::Timeout, "no answer in time");
            } else {
                release_tcp(i);
            }
        }
    }
};

DnsBalancer::DnsBalancer(DnsBalancerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

DnsBalancer::~DnsBalancer() {
    stop();
}

std::vector<std::uint16_t> DnsBalancer::configure(const std::vector<DnsBalancerGroup>& groups,
                                                  const DnsBalancerSettings& settings) {
    return impl_->configure(groups, settings);
}

std::vector<DnsBalancerGroup> DnsBalancer::groups() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->applied;
}

std::vector<DnsProbeResult> DnsBalancer::probe(const std::vector<DnsProbeRequest>& requests,
                                               std::chrono::milliseconds deadline,
                                               const DnsBalancerSettings& settings) {
    return impl_->probe(requests, deadline, settings);
}

DnsBalancerStats DnsBalancer::stats() {
    return impl_->stats();
}

void DnsBalancer::stop() {
    impl_->stop();
}

} // namespace keen_pbr3
