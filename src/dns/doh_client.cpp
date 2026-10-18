#include "doh_client.hpp"

#include "../log/logger.hpp"

#include <curl/curl.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>

namespace keen_pbr3 {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxAnswer = 65535;
constexpr std::size_t kDnsHeaderLen = 12;
// Transfers whose result is being handed over still hold their slot; this many
// spare ones let the callback start a retry.
constexpr std::size_t kSpareTransfers = 4;

template <typename T>
bool setopt(CURL* curl, CURLoption option, T value) {
    return curl_easy_setopt(curl, option, value) == CURLE_OK;
}

bool connection_problem_code(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
            return true;
        default:
            return false;
    }
}

// What went wrong with a transfer that curl could not finish; `os_error` is
// the errno of a failed connect.
DohFailure failure_for_code(CURLcode code, long os_error, bool too_big) {
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
            return DohFailure::Timeout;
        case CURLE_COULDNT_CONNECT:
            return os_error == ECONNREFUSED ? DohFailure::ConnectionRefused
                                            : DohFailure::Unreachable;
        case CURLE_COULDNT_RESOLVE_HOST:
            return DohFailure::NotResolved;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
            return DohFailure::TlsError;
        case CURLE_GOT_NOTHING:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
            return DohFailure::ConnectionClosed;
        case CURLE_WRITE_ERROR:
            return too_big ? DohFailure::BadAnswer : DohFailure::Other;
        default:
            return DohFailure::Other;
    }
}

std::string pin_entry(const std::string& host, std::uint16_t port,
                      const std::vector<std::string>& ips) {
    std::string entry = host + ":" + std::to_string(port) + ":";
    for (std::size_t i = 0; i < ips.size(); ++i) {
        if (i != 0) entry += ',';
        const bool v6 = ips[i].find(':') != std::string::npos;
        entry += v6 ? "[" + ips[i] + "]" : ips[i];
    }
    return entry;
}

} // namespace

struct DohClient::Impl {
    struct Endpoint {
        std::string url;
        std::string host;
        std::uint16_t port{443};
        std::uint32_t mark{0};
        curl_slist* pins{nullptr};
        std::vector<curl_slist*> retired;  // replaced pins that transfers may still read
        bool ready{false};
        std::size_t in_flight{0};
    };

    struct Transfer {
        Impl* owner{nullptr};
        CURL* easy{nullptr};
        bool busy{false};
        bool releasing{false};  // result being handed over: slot not reusable yet
        std::size_t index{0};       // position in `transfers`
        std::uint64_t generation{0};  // bumped by every start()
        std::size_t endpoint{0};
        std::uint64_t user{0};
        std::uint32_t mark{0};
        std::vector<std::uint8_t> body;
        std::vector<std::uint8_t> response;
        bool too_big{false};
        char error[CURL_ERROR_SIZE]{};
    };

    DohClientOptions options;
    DoneFn done_fn;
    void* context;

    CURLM* multi{nullptr};
    curl_slist* headers{nullptr};
    std::deque<Endpoint> endpoints;
    std::deque<Transfer> transfers;
    bool timer_armed{false};
    Clock::time_point timer_at{};

    struct Finished {
        Transfer* transfer;
        std::uint64_t generation;
        CURLcode code;
    };
    std::vector<Finished> finished;

    Impl(const DohClientOptions& opts, DoneFn done, void* ctx)
        : options(opts), done_fn(done), context(ctx) {}

    ~Impl() {
        for (auto& transfer : transfers) {
            if (transfer.easy != nullptr) {
                if (transfer.busy && multi != nullptr) {
                    curl_multi_remove_handle(multi, transfer.easy);
                }
                curl_easy_cleanup(transfer.easy);
            }
        }
        if (multi != nullptr) curl_multi_cleanup(multi);
        for (auto& endpoint : endpoints) free_pins(endpoint);
        if (headers != nullptr) curl_slist_free_all(headers);
    }

    bool init() {
        multi = curl_multi_init();
        if (multi == nullptr) return false;
        curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, &Impl::socket_cb);
        curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, &Impl::timer_cb);
        curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
#ifdef CURLPIPE_MULTIPLEX
        curl_multi_setopt(multi, CURLMOPT_PIPELINING, static_cast<long>(CURLPIPE_MULTIPLEX));
#endif
        curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, 4L);
        curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 8L);
        headers = curl_slist_append(headers, "Content-Type: application/dns-message");
        headers = curl_slist_append(headers, "Accept: application/dns-message");
        if (headers == nullptr) return false;
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        if (info != nullptr) {
            Logger::instance().verbose("DoH client: libcurl {}, {}, HTTP/2 {}", info->version,
                                       info->ssl_version != nullptr ? info->ssl_version : "no TLS",
                                       (info->features & CURL_VERSION_HTTP2) != 0 ? "yes" : "no");
        }
        return true;
    }

    static void free_pins(Endpoint& endpoint) {
        if (endpoint.pins != nullptr) curl_slist_free_all(endpoint.pins);
        endpoint.pins = nullptr;
        for (curl_slist* old : endpoint.retired) curl_slist_free_all(old);
        endpoint.retired.clear();
    }

    // --- epoll glue ---------------------------------------------------------

    static int socket_cb(CURL*, curl_socket_t fd, int what, void* userp, void* socketp) {
        auto* self = static_cast<Impl*>(userp);
        if (what == CURL_POLL_REMOVE) {
            if (socketp != nullptr) {
                ::epoll_ctl(self->options.epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                curl_multi_assign(self->multi, fd, nullptr);
            }
            return 0;
        }
        epoll_event ev{};
        ev.events = ((what & CURL_POLL_IN) != 0 ? EPOLLIN : 0u) |
                    ((what & CURL_POLL_OUT) != 0 ? EPOLLOUT : 0u);
        ev.data.u64 = (static_cast<std::uint64_t>(self->options.tag_kind) << 32) |
                      static_cast<std::uint32_t>(fd);
        if (socketp != nullptr) {
            ::epoll_ctl(self->options.epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        } else {
            ::epoll_ctl(self->options.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
            curl_multi_assign(self->multi, fd, self);
        }
        return 0;
    }

    static int timer_cb(CURLM*, long timeout_ms, void* userp) {
        auto* self = static_cast<Impl*>(userp);
        if (timeout_ms < 0) {
            self->timer_armed = false;
        } else {
            self->timer_armed = true;
            self->timer_at = Clock::now() + std::chrono::milliseconds(timeout_ms);
        }
        return 0;
    }

    // --- transfers ------------------------------------------------------------

    static std::size_t write_cb(char* data, std::size_t size, std::size_t count, void* opaque) {
        auto* transfer = static_cast<Transfer*>(opaque);
        const std::size_t total = size * count;
        if (transfer->response.size() + total > kMaxAnswer) {
            transfer->too_big = true;
            return 0;
        }
        transfer->response.insert(transfer->response.end(),
                                  reinterpret_cast<const std::uint8_t*>(data),
                                  reinterpret_cast<const std::uint8_t*>(data) + total);
        return total;
    }

    static int sockopt_cb(void* opaque, curl_socket_t fd, curlsocktype purpose) {
        if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKOPT_OK;
        auto* transfer = static_cast<Transfer*>(opaque);
        if (transfer->mark == 0) return CURL_SOCKOPT_OK;
        const std::uint32_t mark = transfer->mark;
        if (::setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)) == 0) {
            return CURL_SOCKOPT_OK;
        }
        // Never send the query out unmarked: it would leave the router on the
        // wrong path.
        Logger::instance().warn("DoH client: cannot mark the connection ({}): {}", mark,
                                std::strerror(errno));
        return CURL_SOCKOPT_ERROR;
    }

    bool configure_easy(Transfer& transfer) {
        CURL* easy = transfer.easy;
        bool ok = setopt(easy, CURLOPT_NOSIGNAL, 1L) &&
                  setopt(easy, CURLOPT_WRITEFUNCTION, &Impl::write_cb) &&
                  setopt(easy, CURLOPT_WRITEDATA, &transfer) &&
                  setopt(easy, CURLOPT_SOCKOPTFUNCTION, &Impl::sockopt_cb) &&
                  setopt(easy, CURLOPT_SOCKOPTDATA, &transfer) &&
                  setopt(easy, CURLOPT_ERRORBUFFER, transfer.error) &&
                  setopt(easy, CURLOPT_HTTPHEADER, headers) &&
                  setopt(easy, CURLOPT_USERAGENT, "keen-pbr") &&
                  setopt(easy, CURLOPT_POST, 1L) &&
                  setopt(easy, CURLOPT_FOLLOWLOCATION, 0L) &&
                  setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L) &&
                  setopt(easy, CURLOPT_TCP_KEEPIDLE, 30L) &&
                  setopt(easy, CURLOPT_PIPEWAIT, 1L) &&
                  setopt(easy, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_2TLS));
        if (!ok) return false;
#if LIBCURL_VERSION_NUM >= 0x075500
        ok = setopt(easy, CURLOPT_PROTOCOLS_STR, options.allow_plain_http ? "http,https" : "https");
#else
        ok = setopt(easy, CURLOPT_PROTOCOLS,
                    static_cast<long>(options.allow_plain_http ? (CURLPROTO_HTTP | CURLPROTO_HTTPS)
                                                               : CURLPROTO_HTTPS));
#endif
        if (!ok) return false;
        if (!options.ca_path.empty()) {
            ok = setopt(easy, options.ca_is_directory ? CURLOPT_CAPATH : CURLOPT_CAINFO,
                        options.ca_path.c_str());
        }
        if (ok) {
            transfer.body.reserve(512);
            transfer.response.reserve(2048);
        }
        return ok;
    }

    Transfer* acquire() {
        for (auto& transfer : transfers) {
            if (!transfer.busy && !transfer.releasing) return &transfer;
        }
        if (transfers.size() >= options.max_in_flight + kSpareTransfers) return nullptr;
        Transfer& fresh = transfers.emplace_back();
        fresh.owner = this;
        fresh.index = transfers.size() - 1;
        fresh.easy = curl_easy_init();
        if (fresh.easy == nullptr || !configure_easy(fresh)) {
            if (fresh.easy != nullptr) curl_easy_cleanup(fresh.easy);
            transfers.pop_back();
            return nullptr;
        }
        return &fresh;
    }

    std::size_t in_flight() const {
        return static_cast<std::size_t>(std::count_if(
            transfers.begin(), transfers.end(), [](const Transfer& t) { return t.busy; }));
    }

    void release(Transfer& transfer) {
        Endpoint& endpoint = endpoints[transfer.endpoint];
        if (endpoint.in_flight > 0) --endpoint.in_flight;
        if (endpoint.in_flight == 0) {
            for (curl_slist* old : endpoint.retired) curl_slist_free_all(old);
            endpoint.retired.clear();
        }
    }

    // --- completion --------------------------------------------------------------

    void collect_finished() {
        int pending = 0;
        while (CURLMsg* msg = curl_multi_info_read(multi, &pending)) {
            if (msg->msg != CURLMSG_DONE) continue;
            char* priv = nullptr;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
            if (priv == nullptr) continue;
            auto* transfer = reinterpret_cast<Transfer*>(priv);
            finished.push_back({transfer, transfer->generation, msg->data.result});
        }
    }

    void dispatch_finished() {
        // The callback may start transfers, which can finish only in a later
        // action, so a plain loop over a snapshot is enough.
        std::vector<Finished> batch;
        batch.swap(finished);
        for (const Finished& item : batch) {
            Transfer& transfer = *item.transfer;
            // Cancelled meanwhile (and maybe reused for another query).
            if (!transfer.busy || transfer.generation != item.generation) continue;
            curl_multi_remove_handle(multi, transfer.easy);
            transfer.busy = false;
            transfer.releasing = true;
            release(transfer);

            DohResult result;
            if (item.code == CURLE_OK) {
                long status = 0;
                curl_easy_getinfo(transfer.easy, CURLINFO_RESPONSE_CODE, &status);
                result.http_status = status;
                if (status != 200) {
                    result.error = "HTTP status " + std::to_string(status);
                    result.failure = DohFailure::HttpError;
                } else if (transfer.response.size() < kDnsHeaderLen) {
                    result.error = "answer is not a DNS message";
                    result.failure = DohFailure::BadAnswer;
                } else {
                    result.ok = true;
                    result.body = transfer.response.data();
                    result.body_len = transfer.response.size();
                }
            } else {
                result.error = transfer.error[0] != '\0' ? transfer.error : curl_easy_strerror(item.code);
                result.connection_problem = connection_problem_code(item.code);
                long os_error = 0;
                curl_easy_getinfo(transfer.easy, CURLINFO_OS_ERRNO, &os_error);
                result.failure = failure_for_code(item.code, os_error, transfer.too_big);
            }
            done_fn(context, transfer.user, result);
            transfer.releasing = false;
        }
    }

    void socket_action(curl_socket_t fd, int mask) {
        int running = 0;
        curl_multi_socket_action(multi, fd, mask, &running);
        collect_finished();
        dispatch_finished();
    }
};

DohClient::DohClient(const DohClientOptions& options, DoneFn done, void* context)
    : impl_(std::make_unique<Impl>(options, done, context)) {
    if (!impl_->init()) {
        impl_.reset();
    }
}

DohClient::~DohClient() = default;

bool DohClient::valid() const {
    return impl_ != nullptr;
}

std::size_t DohClient::add_endpoint(const std::string& url, std::uint32_t fwmark) {
    if (!impl_) return kNoEndpoint;
    const bool https = url.rfind("https://", 0) == 0;
    const bool http = url.rfind("http://", 0) == 0;
    if (!https && !(http && impl_->options.allow_plain_http)) return kNoEndpoint;
    CURLU* parsed = curl_url();
    if (parsed == nullptr) return kNoEndpoint;
    std::size_t result = kNoEndpoint;
    char* host = nullptr;
    char* port = nullptr;
    if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) == CURLUE_OK) {
        Impl::Endpoint& endpoint = impl_->endpoints.emplace_back();
        endpoint.url = url;
        endpoint.host = host;
        // curl keeps IPv6 literals in brackets.
        if (endpoint.host.size() > 2 && endpoint.host.front() == '[') {
            endpoint.host = endpoint.host.substr(1, endpoint.host.size() - 2);
        }
        endpoint.port = static_cast<std::uint16_t>(std::strtoul(port, nullptr, 10));
        endpoint.mark = fwmark;
        result = impl_->endpoints.size() - 1;
    }
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(parsed);
    return result;
}

void DohClient::set_addresses(std::size_t endpoint_id, const std::vector<std::string>& ips) {
    if (!impl_ || endpoint_id >= impl_->endpoints.size()) return;
    Impl::Endpoint& endpoint = impl_->endpoints[endpoint_id];
    curl_slist* fresh = nullptr;
    if (!ips.empty()) {
        fresh = curl_slist_append(nullptr, pin_entry(endpoint.host, endpoint.port, ips).c_str());
    }
    if (endpoint.pins != nullptr) {
        if (endpoint.in_flight == 0) {
            curl_slist_free_all(endpoint.pins);
        } else {
            endpoint.retired.push_back(endpoint.pins);
        }
    }
    endpoint.pins = fresh;
    endpoint.ready = fresh != nullptr;
}

const std::string& DohClient::endpoint_host(std::size_t endpoint) const {
    return impl_->endpoints[endpoint].host;
}

bool DohClient::endpoint_ready(std::size_t endpoint) const {
    return impl_ && endpoint < impl_->endpoints.size() && impl_->endpoints[endpoint].ready;
}

void DohClient::clear_endpoints() {
    if (!impl_) return;
    for (auto& transfer : impl_->transfers) {
        if (transfer.busy) {
            curl_multi_remove_handle(impl_->multi, transfer.easy);
            transfer.busy = false;
        }
    }
    for (auto& endpoint : impl_->endpoints) Impl::free_pins(endpoint);
    impl_->endpoints.clear();
}

std::size_t DohClient::start(std::size_t endpoint_id, const std::uint8_t* query, std::size_t len,
                             std::chrono::milliseconds timeout, std::uint64_t user) {
    if (!impl_ || endpoint_id >= impl_->endpoints.size() || len < kDnsHeaderLen) {
        return kNoTransfer;
    }
    Impl::Endpoint& endpoint = impl_->endpoints[endpoint_id];
    if (!endpoint.ready) return kNoTransfer;
    if (impl_->in_flight() >= impl_->options.max_in_flight) return kNoTransfer;
    Impl::Transfer* transfer = impl_->acquire();
    if (transfer == nullptr) return kNoTransfer;

    transfer->body.assign(query, query + len);
    transfer->body[0] = 0;  // RFC 8484: id 0 keeps the message cache-friendly
    transfer->body[1] = 0;
    transfer->response.clear();
    transfer->too_big = false;
    transfer->error[0] = '\0';
    transfer->endpoint = endpoint_id;
    transfer->user = user;
    transfer->mark = endpoint.mark;
    ++transfer->generation;

    CURL* easy = transfer->easy;
    const bool ok =
        setopt(easy, CURLOPT_URL, endpoint.url.c_str()) &&
        setopt(easy, CURLOPT_RESOLVE, endpoint.pins) &&
        setopt(easy, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(transfer->body.data())) &&
        setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(transfer->body.size())) &&
        setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()) + 500L) &&
        setopt(easy, CURLOPT_PRIVATE, reinterpret_cast<char*>(transfer));
    if (!ok || curl_multi_add_handle(impl_->multi, easy) != CURLM_OK) {
        return kNoTransfer;
    }
    transfer->busy = true;
    ++endpoint.in_flight;
    return transfer->index;
}

void DohClient::cancel(std::size_t transfer_id) {
    if (!impl_ || transfer_id >= impl_->transfers.size()) return;
    Impl::Transfer& transfer = impl_->transfers[transfer_id];
    if (!transfer.busy) return;
    curl_multi_remove_handle(impl_->multi, transfer.easy);
    transfer.busy = false;
    impl_->release(transfer);
}

void DohClient::handle_socket_event(int fd, std::uint32_t events) {
    if (!impl_) return;
    int mask = 0;
    if ((events & EPOLLIN) != 0) mask |= CURL_CSELECT_IN;
    if ((events & EPOLLOUT) != 0) mask |= CURL_CSELECT_OUT;
    if ((events & (EPOLLERR | EPOLLHUP)) != 0) mask |= CURL_CSELECT_ERR;
    impl_->socket_action(fd, mask);
}

void DohClient::handle_timeout() {
    if (!impl_) return;
    impl_->timer_armed = false;
    impl_->socket_action(CURL_SOCKET_TIMEOUT, 0);
}

int DohClient::next_timeout_ms() const {
    if (!impl_ || !impl_->timer_armed) return -1;
    const auto now = Clock::now();
    if (impl_->timer_at <= now) return 0;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(impl_->timer_at - now).count() + 1);
}

} // namespace keen_pbr3
