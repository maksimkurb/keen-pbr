#include "dns_balancer.hpp"

#include "../log/logger.hpp"

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
#include <mutex>
#include <thread>

namespace keen_pbr3 {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxListeners = kDnsBalancerMaxGroups;
constexpr std::size_t kUdpSlots = 128;
constexpr std::size_t kTcpSessions = 16;
constexpr std::size_t kUdpQueryMax = 1500;
constexpr std::size_t kUdpAnswerMax = 16384;
constexpr std::size_t kTcpQueryMax = 4096;
constexpr std::size_t kTcpAnswerMax = 16384;
constexpr std::size_t kDnsHeaderLen = 12;
constexpr int kAcceptPerWake = 8;
constexpr int kUdpPerWake = 32;

enum class Kind : std::uint32_t { Wake, ListenUdp, ListenTcp, UdpUp, TcpClient, TcpUp };

std::uint64_t make_tag(Kind kind, std::size_t index) {
    return (static_cast<std::uint64_t>(kind) << 32) | static_cast<std::uint32_t>(index);
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool make_addr(const std::string& ip, std::uint16_t port, sockaddr_in& out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return ::inet_pton(AF_INET, ip.c_str(), &out.sin_addr) == 1;
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

struct Upstream {
    sockaddr_in addr{};
    Clock::time_point dead_until{};
};

struct Listener {
    bool used{false};
    std::uint16_t port{0};
    int udp_fd{-1};
    int tcp_fd{-1};
    std::vector<Upstream> upstreams;
    std::uint32_t next{0};
    std::vector<DnsBalancerUpstream> spec;
};

// One in-flight UDP query: an upstream socket of its own and the query bytes.
struct UdpSlot {
    bool used{false};
    std::size_t listener{0};
    int up_fd{-1};
    sockaddr_in client{};
    std::uint16_t len{0};
    std::array<std::uint8_t, kUdpQueryMax> query{};
    std::uint32_t start{0};
    std::uint32_t tried{0};
    std::size_t current{0};
    bool ignore_dead{false};
    Clock::time_point deadline{};
};

enum class TcpState : std::uint8_t { ReadQuery, Connect, ReadAnswer, WriteAnswer };

struct TcpSession {
    bool used{false};
    std::size_t listener{0};
    TcpState state{TcpState::ReadQuery};
    int client_fd{-1};
    int up_fd{-1};
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
    Clock::time_point deadline{};
};

} // namespace

struct DnsBalancer::Impl {
    explicit Impl(DnsBalancerOptions opts) : options(std::move(opts)) {}

    DnsBalancerOptions options;

    // Control side.
    mutable std::mutex mutex;
    std::condition_variable applied_cv;
    std::vector<DnsBalancerGroup> pending;
    std::vector<DnsBalancerGroup> applied;
    std::vector<std::uint16_t> failed;
    std::uint64_t requested_generation{0};
    std::uint64_t applied_generation{0};
    std::thread thread;
    bool stopping{false};

    // Loop side: touched by the thread only (and by stop() after join).
    int epoll_fd{-1};
    int wake_fd{-1};
    std::array<Listener, kMaxListeners> listeners;
    std::vector<UdpSlot> udp_slots;
    std::vector<TcpSession> tcp_sessions;
    std::array<std::uint8_t, kUdpAnswerMax> udp_answer{};

    // --- lifecycle -------------------------------------------------------

    bool start_thread_locked() {
        if (thread.joinable()) {
            return true;
        }
        epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
        wake_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (epoll_fd < 0 || wake_fd < 0) {
            Logger::instance().warn("DNS balancer: cannot create epoll/eventfd: {}",
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
        {
            std::lock_guard<std::mutex> lock(mutex);
            applied.clear();
            applied_generation = requested_generation;
        }
        applied_cv.notify_all();
        close_fd(epoll_fd);
        close_fd(wake_fd);
    }

    // --- control thread ---------------------------------------------------

    std::vector<std::uint16_t> configure(const std::vector<DnsBalancerGroup>& groups) {
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
        if (groups == applied) {
            return failed;
        }
        pending = groups;
        const std::uint64_t generation = ++requested_generation;
        wake();
        applied_cv.wait(lock, [&] { return applied_generation >= generation; });
        return failed;
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
                Logger::instance().warn("DNS balancer: epoll_wait failed: {}",
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
                }
            }
            expire(Clock::now());
            if (reconfigure) {
                std::uint64_t counter = 0;
                [[maybe_unused]] const ssize_t r = ::read(wake_fd, &counter, sizeof(counter));
                if (should_stop()) {
                    break;
                }
                apply_pending();
            } else if (should_stop()) {
                break;
            }
        }
        for (std::size_t i = 0; i < kMaxListeners; ++i) {
            drop_listener(i);
        }
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
        if (earliest == Clock::time_point::max()) {
            return -1;
        }
        if (earliest <= now) {
            return 0;
        }
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now).count() + 1);
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
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            groups = pending;
            generation = requested_generation;
        }
        std::vector<std::uint16_t> bind_failed;

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
        {
            std::lock_guard<std::mutex> lock(mutex);
            applied = groups;
            failed = std::move(bind_failed);
            applied_generation = generation;
        }
        applied_cv.notify_all();
    }

    void set_upstreams(Listener& listener, const DnsBalancerGroup& group) {
        std::vector<Upstream> fresh;
        fresh.reserve(group.upstreams.size());
        for (const auto& spec : group.upstreams) {
            Upstream upstream;
            if (!make_addr(spec.ip, spec.port, upstream.addr)) {
                Logger::instance().warn("DNS balancer: ignoring upstream '{}'", spec.ip);
                continue;
            }
            for (const auto& old : listener.upstreams) {
                if (old.addr.sin_addr.s_addr == upstream.addr.sin_addr.s_addr &&
                    old.addr.sin_port == upstream.addr.sin_port) {
                    upstream.dead_until = old.dead_until;
                }
            }
            fresh.push_back(upstream);
        }
        listener.upstreams = std::move(fresh);
        listener.spec = group.upstreams;
    }

    bool open_listener(std::size_t index, std::uint16_t port) {
        sockaddr_in addr{};
        if (!make_addr(options.listen_ip, port, addr)) {
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
            Logger::instance().warn("DNS balancer: cannot listen on {}:{}: {}", options.listen_ip,
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
            if (ignore_dead || listener.upstreams[index].dead_until <= now) {
                chosen = index;
                return true;
            }
        }
        return false;
    }

    static bool all_dead(const Listener& listener, Clock::time_point now) {
        return std::all_of(listener.upstreams.begin(), listener.upstreams.end(),
                           [&](const Upstream& u) { return u.dead_until > now; });
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

    // --- UDP --------------------------------------------------------------------

    void release_udp(UdpSlot& slot) {
        close_fd(slot.up_fd);
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
            if (len < kDnsHeaderLen || len > kUdpQueryMax || (buf[2] & 0x80) != 0) {
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
        std::size_t chosen = 0;
        while (pick(listener, slot.start, slot.tried, slot.ignore_dead, now, chosen)) {
            const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                break;
            }
            const sockaddr_in& target = listener.upstreams[chosen].addr;
            if (::connect(fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != 0 ||
                ::send(fd, slot.query.data(), slot.len, MSG_NOSIGNAL) != slot.len) {
                ::close(fd);
                mark_dead(listener, chosen, now);
                continue;
            }
            slot.up_fd = fd;
            slot.current = chosen;
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
            mark_dead(listener, slot.current, now);  // e.g. connection refused
            udp_attempt(slot, now);
            return;
        }
        const auto len = static_cast<std::size_t>(n);
        if (len < kDnsHeaderLen || udp_answer[0] != slot.query[0] || udp_answer[1] != slot.query[1] ||
            (udp_answer[2] & 0x80) == 0) {
            return;  // not the answer to this query; keep waiting
        }
        if (is_servfail(udp_answer.data()) && slot.tried < listener.upstreams.size()) {
            udp_attempt(slot, now);
            return;
        }
        mark_alive(listener, slot.current);
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
                release_tcp(index);
                return;
            }
            if (session.query_got == 2 + message) {
                break;
            }
        }
        Listener& listener = listeners[session.listener];
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
        tcp_begin_write(index);
    }

    void tcp_attempt(std::size_t index, Clock::time_point now) {
        TcpSession& session = tcp_sessions[index];
        Listener& listener = listeners[session.listener];
        close_fd(session.up_fd);
        std::size_t chosen = 0;
        while (pick(listener, session.start, session.tried, session.ignore_dead, now, chosen)) {
            const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                break;
            }
            const sockaddr_in& target = listener.upstreams[chosen].addr;
            const int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
            if (rc != 0 && errno != EINPROGRESS) {
                ::close(fd);
                mark_dead(listener, chosen, now);
                continue;
            }
            session.up_fd = fd;
            session.current = chosen;
            session.state = TcpState::Connect;
            session.answer_got = 0;
            session.answer_len = 0;
            session.deadline = now + options.tcp_attempt_timeout;
            epoll_add(fd, EPOLLOUT, Kind::TcpUp, index);
            return;
        }
        tcp_servfail(index);
    }

    void tcp_fail_attempt(std::size_t index, Clock::time_point now) {
        TcpSession& session = tcp_sessions[index];
        mark_dead(listeners[session.listener], session.current, now);
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
                tcp_fail_attempt(index, now);
                return;
            }
            const std::size_t total = 2 + read_be16(session.query.data());
            if (::send(session.up_fd, session.query.data(), total, MSG_NOSIGNAL) !=
                static_cast<ssize_t>(total)) {
                tcp_fail_attempt(index, now);
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
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                tcp_fail_attempt(index, now);
                return;
            }
            if (n < 0) {
                return;
            }
            session.answer_got += static_cast<std::size_t>(n);
            if (session.answer_got >= 2 && session.answer_len == 0) {
                const std::size_t message = read_be16(session.answer.data());
                if (message < kDnsHeaderLen || message > kTcpAnswerMax) {
                    tcp_fail_attempt(index, now);
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
            tcp_fail_attempt(index, now);
            return;
        }
        if (is_servfail(session.answer.data() + 2) && session.tried < listener.upstreams.size()) {
            tcp_attempt(index, now);
            return;
        }
        mark_alive(listener, session.current);
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

    // --- deadlines ------------------------------------------------------------------

    void expire(Clock::time_point now) {
        for (auto& slot : udp_slots) {
            if (slot.used && slot.deadline <= now) {
                mark_dead(listeners[slot.listener], slot.current, now);
                udp_attempt(slot, now);
            }
        }
        for (std::size_t i = 0; i < tcp_sessions.size(); ++i) {
            TcpSession& session = tcp_sessions[i];
            if (!session.used || session.deadline > now) continue;
            if (session.state == TcpState::Connect || session.state == TcpState::ReadAnswer) {
                tcp_fail_attempt(i, now);
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

std::vector<std::uint16_t> DnsBalancer::configure(const std::vector<DnsBalancerGroup>& groups) {
    return impl_->configure(groups);
}

std::vector<DnsBalancerGroup> DnsBalancer::groups() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->applied;
}

void DnsBalancer::stop() {
    impl_->stop();
}

} // namespace keen_pbr3
