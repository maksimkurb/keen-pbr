#include "rci_client.hpp"

#include "../http/http_client.hpp"
#include "../http/http_transport.hpp"
#include "../log/logger.hpp"
#include "../util/safe_exec.hpp"

#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace keen_pbr3 {

namespace {

constexpr const char* kDefaultScgiSocket = "/var/run/ndm.scgi.socket";
constexpr const char* kDefaultHttpBase = "http://127.0.0.1:79";
constexpr const char* kTokenHeader = "X-Ndma-Tkn";
// user-data of the tokens this service creates; stale ones are deleted by it.
constexpr const char* kTokenName = "keen-pbr";
// Room for the status line and headers on top of the body limit.
constexpr std::size_t kScgiHeaderAllowance = std::size_t{16} * 1024U;
// A failed token creation is not retried sooner than this.
constexpr auto kProvisionRetryInterval = std::chrono::minutes(10);

enum class RciMode { kUnknown, kHttpPlain, kHttpToken, kScgi };

RciNdmcResult run_ndmc(const std::vector<std::string>& args) {
    SafeExecTimeouts timeouts;
    timeouts.timeout = std::chrono::seconds(5);
    timeouts.kill_grace = std::chrono::seconds(1);
    const auto result = safe_exec_capture_with_timeouts(args, timeouts, true,
                                                        std::size_t{64} * 1024U, true);
    return {result.exit_code, result.stdout_output};
}

// Where requests go and how.  Copied out of the state so no lock is held
// while a request or a process runs.
struct RciEnv {
    std::string scgi_socket;
    std::string http_base;
    std::shared_ptr<HttpTransport> transport;
    RciNdmcFn ndmc;
};

// Two locks, always taken in this order:
//   search_mutex  serialises finding a way in and creating tokens (slow: it
//                 runs requests and ndmc).  Guards the fields marked [search].
//   mutex         guards the fields marked [shared]; held only to copy or set
//                 them, never across I/O, so a request on a known way in never
//                 waits behind a search.
struct RciState {
    std::mutex search_mutex;
    std::mutex mutex;

    RciMode mode{RciMode::kUnknown}; // [shared]
    std::string token;               // [shared]
    std::string scgi_socket{kDefaultScgiSocket}; // [shared]
    std::string http_base{kDefaultHttpBase};     // [shared]
    std::shared_ptr<HttpTransport> transport;    // [shared]
    RciNdmcFn ndmc{run_ndmc};                    // [shared]

    std::string token_file;                   // [search] where the token is kept
    bool may_create_token{false};             // [search] only the service creates tokens
    bool provision_blocked{false};            // [search] retrying in this run cannot help
    std::optional<bool> firmware_wants_token; // [search] KeeneticOS 5.2+, decided once known
    std::optional<std::chrono::steady_clock::time_point> last_provision_attempt; // [search]
    std::string last_provision_error;         // [search]
};

struct Snapshot {
    RciMode mode{RciMode::kUnknown};
    std::string token;
    RciEnv env;
};

Snapshot take_snapshot(RciState& state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.transport) state.transport = default_http_transport();
    return {state.mode, state.token,
            {state.scgi_socket, state.http_base, state.transport, state.ndmc}};
}

void publish_mode(RciState& state, RciMode mode) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.mode = mode;
}

void publish_token(RciState& state, const std::string& token) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.token = token;
}

RciState& rci_state() {
    static RciState state;
    return state;
}

// ---------------------------------------------------------------- helpers

std::string lower_copy(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string trim_copy(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool valid_token(const std::string& token) {
    if (token.size() < 16 || token.size() > 512) return false;
    for (const char c : token) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 &&
            std::strchr("._~+/=-", c) == nullptr) {
            return false;
        }
    }
    return true;
}

std::string first_line(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (!line.empty()) return line.size() > 200 ? line.substr(0, 200) : line;
    }
    return "no output";
}

std::optional<int> parse_status_code(const std::string& text) {
    std::size_t i = 0;
    int code = 0;
    while (i < text.size() && i < 3 && std::isdigit(static_cast<unsigned char>(text[i]))) {
        code = code * 10 + (text[i] - '0');
        ++i;
    }
    if (i != 3) return std::nullopt;
    return code;
}

bool is_auth_failure(long status) { return status == 401 || status == 403; }

// ---------------------------------------------------------------- SCGI

// The socket does not exist: not an error worth reporting.
class ScgiUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ScgiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) ::close(fd_);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

std::string errno_text(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

int remaining_ms(std::chrono::steady_clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return left.count() > 0 ? static_cast<int>(left.count()) : 0;
}

void wait_for(int fd, short events, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        const int timeout = remaining_ms(deadline);
        if (timeout == 0) throw ScgiError("timed out");
        pollfd pfd{fd, events, 0};
        const int rc = ::poll(&pfd, 1, timeout);
        if (rc > 0) return;
        if (rc == 0) throw ScgiError("timed out");
        if (errno != EINTR) throw ScgiError(errno_text("poll"));
    }
}

rci_detail::ScgiResponse scgi_request(const std::string& socket_path, const std::string& method,
                                      const std::string& uri, const std::string& body,
                                      const RciRequestLimits& limits) {
    struct stat st{};
    if (::stat(socket_path.c_str(), &st) != 0 || !S_ISSOCK(st.st_mode)) {
        throw ScgiUnavailable("no socket at " + socket_path);
    }
    sockaddr_un addr{};
    if (socket_path.size() >= sizeof(addr.sun_path)) throw ScgiError("socket path is too long");
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) throw ScgiError(errno_text("socket"));

    // A blocking unix-socket connect waits at most SO_SNDTIMEO.
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(limits.timeout.count());
    ::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw ScgiError(errno_text("connect"));
    }
    const int flags = ::fcntl(fd.get(), F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK) != 0) {
        throw ScgiError(errno_text("fcntl"));
    }

    const std::string request = rci_detail::build_scgi_request(method, uri, body);
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = ::send(fd.get(), request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            wait_for(fd.get(), POLLOUT, deadline);
        } else if (n < 0 && errno != EINTR) {
            throw ScgiError(errno_text("send"));
        }
    }
    // Half-close like socat does; NDM reads the request to its end.
    ::shutdown(fd.get(), SHUT_WR);

    std::string raw;
    std::vector<char> buffer(std::size_t{16} * 1024U);
    for (;;) {
        if (auto parsed = rci_detail::parse_scgi_response(raw, false)) return std::move(*parsed);
        const ssize_t n = ::recv(fd.get(), buffer.data(), buffer.size(), 0);
        if (n > 0) {
            raw.append(buffer.data(), static_cast<std::size_t>(n));
            if (raw.size() > limits.max_response_size + kScgiHeaderAllowance) {
                throw ScgiError("response exceeds " + std::to_string(limits.max_response_size) + " bytes");
            }
        } else if (n == 0 || errno == ECONNRESET) {
            // End of stream; NDM may reset the connection after the response.
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            wait_for(fd.get(), POLLIN, deadline);
        } else if (errno != EINTR) {
            throw ScgiError(errno_text("recv"));
        }
    }
    if (auto parsed = rci_detail::parse_scgi_response(raw, true)) return std::move(*parsed);
    throw ScgiError(raw.empty() ? "empty response" : "incomplete response");
}

// ---------------------------------------------------------------- attempts

struct Attempt {
    bool ok{false};
    bool unavailable{false}; // SCGI only: there is no socket
    long status{0};          // 0: no HTTP status (transport failure)
    std::string body;
    std::string error;
};

Attempt http_attempt(const RciEnv& env, const std::string& method, const std::string& uri,
                     const std::string& body, const RciRequestLimits& limits,
                     const std::string& token) {
    HttpTransportRequest request;
    request.url = env.http_base + uri;
    request.timeout_ms = static_cast<long>(limits.timeout.count() * 1000);
    request.user_agent = "keen-pbr/" KEEN_PBR3_VERSION_STRING;
    request.max_redirects = 0;
    request.max_response_size = limits.max_response_size;
    request.method = method;
    request.body = body;
    if (method == "POST") request.headers.emplace_back("Content-Type: application/json");
    if (!token.empty()) request.headers.push_back(std::string(kTokenHeader) + ": " + token);

    Attempt attempt;
    try {
        auto response = env.transport->perform(request);
        attempt.status = response.status_code;
        if (response.status_code >= 200 && response.status_code < 300) {
            attempt.ok = true;
            attempt.body = std::move(response.body);
            return attempt;
        }
        attempt.error = "HTTP error " + std::to_string(response.status_code);
        const auto detail = response.headers.find("x-detail");
        if (detail != response.headers.end() && !detail->second.empty()) {
            attempt.error += " (" + detail->second + ")";
        }
    } catch (const HttpTransportError& e) {
        attempt.error = e.what();
    }
    return attempt;
}

Attempt scgi_attempt(const RciEnv& env, const std::string& method, const std::string& uri,
                     const std::string& body, const RciRequestLimits& limits) {
    Attempt attempt;
    try {
        auto response = scgi_request(env.scgi_socket, method, uri, body, limits);
        if (response.status == 200 && nlohmann::json::accept(response.body)) {
            attempt.ok = true;
            attempt.body = std::move(response.body);
            return attempt;
        }
        attempt.error = response.status == 200 ? "status 200 without a JSON body"
                                                : "status " + std::to_string(response.status);
        if (!response.detail.empty()) attempt.error += " (" + response.detail + ")";
    } catch (const ScgiUnavailable&) {
        attempt.unavailable = true;
    } catch (const std::exception& e) {
        attempt.error = e.what();
    }
    return attempt;
}

// ---------------------------------------------------------------- tokens

void save_token(const std::string& path, const std::string& token) {
    {
        std::ifstream existing(path);
        std::string current;
        if (existing && std::getline(existing, current) && trim_copy(current) == token) return;
    }
    // A leftover file or symlink must not decide who can read the token or
    // where it goes: remove it and create the file ourselves.
    const std::string temporary = path + ".tmp";
    (void)::unlink(temporary.c_str());
    const int fd = ::open(temporary.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error(errno_text(("cannot write " + temporary).c_str()));
    const std::string content = token + "\n";
    bool written = ::fchmod(fd, 0600) == 0;
    for (std::size_t done = 0; written && done < content.size();) {
        const ssize_t n = ::write(fd, content.data() + done, content.size() - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            written = false;
        }
    }
    const bool synced = written && ::fsync(fd) == 0;
    ::close(fd);
    if (!written || !synced || std::rename(temporary.c_str(), path.c_str()) != 0) {
        const std::string error = errno_text(("cannot write " + path).c_str());
        ::unlink(temporary.c_str());
        throw std::runtime_error(error);
    }
}

// Remember a failure that retrying in this run cannot fix.
std::string block_provisioning(RciState& state, std::string error) {
    state.provision_blocked = true;
    state.last_provision_error = std::move(error);
    return state.last_provision_error;
}

std::string describe_generate_failure(RciState& state, const RciNdmcResult& result) {
    const std::string output = lower_copy(result.output);
    if (output.find("argument parse error") != std::string::npos ||
        output.find("no such command") != std::string::npos) {
        return block_provisioning(state, "this KeeneticOS version has no RCI access tokens");
    }
    if (output.find("invalid username") != std::string::npos) {
        return block_provisioning(
            state, "the admin user needs the \"Web configurator\" permission to create an RCI "
                   "access token");
    }
    if (result.exit_code == 127 || result.exit_code < 0) {
        return block_provisioning(state, "ndmc is not available");
    }
    state.last_provision_error =
        "ndmc could not create an RCI access token: " + first_line(result.output);
    return state.last_provision_error;
}

// Delete keen-pbr tokens other than `keep_id`, one at a time, listing again
// before every delete so only an id that still belongs to keen-pbr goes.
// Their values cannot be read back, so they are useless to this service.
// Only the service creates tokens, so these are its own earlier ones.  Bounded
// in rounds and in time: a hanging ndmc must not stall callers for long.
void remove_stale_tokens(const RciEnv& env, const std::string& keep_id) {
    constexpr auto kBudget = std::chrono::seconds(12);
    const auto deadline = std::chrono::steady_clock::now() + kBudget;
    std::vector<std::string> tried;
    for (int round = 0; round < 8 && std::chrono::steady_clock::now() < deadline; ++round) {
        const auto listing = env.ndmc({"ndmc", "-c", "show authentication token"});
        std::string stale;
        for (const auto& id : rci_detail::parse_token_ids(listing.output, kTokenName)) {
            if (id != keep_id && std::find(tried.begin(), tried.end(), id) == tried.end()) {
                stale = id;
                break;
            }
        }
        if (stale.empty()) return;
        tried.push_back(stale);
        (void)env.ndmc({"ndmc", "-c", "authentication token delete " + stale});
    }
}

// Create a token and drop the keen-pbr tokens made before it.  The new token
// is saved before anything is deleted, so a working one always exists.  The
// token is published (and returned in `token`) as soon as it exists.
// Returns an error message, or no value on success.  Needs search_mutex.
std::optional<std::string> provision_token(RciState& state, const RciEnv& env,
                                           std::string& token) {
    if (!state.may_create_token || state.token_file.empty()) {
        return std::string("an RCI access token is required; the keen-pbr service creates it");
    }
    if (state.provision_blocked) return state.last_provision_error;
    const auto now = std::chrono::steady_clock::now();
    if (state.last_provision_attempt && now - *state.last_provision_attempt < kProvisionRetryInterval) {
        return state.last_provision_error.empty()
                   ? std::string("an RCI access token was created moments ago; not creating another")
                   : state.last_provision_error;
    }
    state.last_provision_attempt = now;

    const auto generated = env.ndmc(
        {"ndmc", "-c", std::string("authentication token generate ") + kTokenName});
    const auto generated_token = rci_detail::parse_generated_token(generated.output);
    if (!generated_token) return describe_generate_failure(state, generated);

    token = generated_token->value;
    publish_token(state, token);
    try {
        save_token(state.token_file, token);
    } catch (const std::exception& e) {
        // The token still works for this run.
        Logger::instance().warn("Keenetic RCI: cannot save the access token: {}", e.what());
    }
    remove_stale_tokens(env, generated_token->id);
    Logger::instance().info("Keenetic RCI: created access token \"{}\" for user admin", kTokenName);
    state.last_provision_error.clear();
    return std::nullopt;
}

// NDM refused (401/403) a token it had just created; making more of them will
// not help, so stop for this run.  A timeout or another error says nothing
// about the token and does not come here.
std::string reject_fresh_token(RciState& state, std::string& token, const Attempt& attempt) {
    token.clear();
    publish_token(state, token);
    return block_provisioning(state, "KeeneticOS refused the RCI access token it had just created (" +
                                         attempt.error + ")");
}

bool release_is_at_least_5_2(const std::string& version_body) {
    try {
        const auto json = nlohmann::json::parse(version_body);
        if (!json.is_object()) return false;
        const auto release = json.find("release");
        if (release == json.end() || !release->is_string()) return false;
        const std::string text = release->get<std::string>(); // "5.02.B.4.0-0"
        const auto dot = text.find('.');
        if (dot == std::string::npos) return false;
        const int major = std::stoi(text.substr(0, dot));
        const int minor = std::stoi(text.substr(dot + 1));
        return major > 5 || (major == 5 && minor >= 2);
    } catch (const std::exception&) {
        return false;
    }
}

// KeeneticOS 5.2 logs a warning for every tokenless RCI request and is
// closing that access, so the service switches to a token there right away.
// Remembered once the version is known; a failed probe is not remembered, so
// the next request asks again.  Needs search_mutex.
std::optional<bool> firmware_wants_token(RciState& state, const RciEnv& env, const std::string& uri,
                                         const std::string& body, const RciRequestLimits& limits) {
    if (!state.may_create_token) return false;
    if (!state.firmware_wants_token) {
        if (uri == "/rci/show/version") {
            state.firmware_wants_token = release_is_at_least_5_2(body);
        } else {
            const auto version = http_attempt(env, "GET", "/rci/show/version", "", limits, "");
            if (!version.ok) return std::nullopt;
            state.firmware_wants_token = release_is_at_least_5_2(version.body);
        }
    }
    return state.firmware_wants_token;
}

// ---------------------------------------------------------------- request

[[noreturn]] void throw_attempt(const Attempt& attempt) {
    throw HttpError(attempt.error, attempt.status);
}

// One request on the way in that worked last time.  Returns the body on
// success.  Throws for an answer that needs no new search (an HTTP error other
// than 401/403, or no HTTP answer at all: a timeout says nothing about the
// token or the mode).  Returns no value when a new search is warranted.
std::optional<std::string> try_known_way(const Snapshot& snap, const std::string& method,
                                         const std::string& uri, const std::string& body,
                                         const RciRequestLimits& limits) {
    switch (snap.mode) {
        case RciMode::kHttpToken:
        case RciMode::kHttpPlain: {
            auto attempt = http_attempt(snap.env, method, uri, body, limits,
                                        snap.mode == RciMode::kHttpToken ? snap.token
                                                                         : std::string());
            if (attempt.ok) return std::move(attempt.body);
            if (!is_auth_failure(attempt.status)) throw_attempt(attempt);
            return std::nullopt;
        }
        case RciMode::kScgi: {
            auto attempt = scgi_attempt(snap.env, method, uri, body, limits);
            if (attempt.ok) return std::move(attempt.body);
            return std::nullopt;
        }
        case RciMode::kUnknown:
            return std::nullopt;
    }
    return std::nullopt;
}

// Find a way in.  Runs under search_mutex, so one thread at a time; others
// keep using the mode that is published.
std::string search_way_in(RciState& state, const std::string& method, const std::string& uri,
                          const std::string& body, const RciRequestLimits& limits) {
    Snapshot snap = take_snapshot(state);
    const RciEnv& env = snap.env;
    std::string token = snap.token;
    const RciMode previous = snap.mode;
    publish_mode(state, RciMode::kUnknown);

    std::vector<std::string> errors;
    long last_status = 0;

    if (!token.empty()) {
        auto attempt = http_attempt(env, method, uri, body, limits, token);
        if (attempt.ok) {
            publish_mode(state, RciMode::kHttpToken);
            return std::move(attempt.body);
        }
        if (attempt.status != 0 && !is_auth_failure(attempt.status)) {
            publish_mode(state, RciMode::kHttpToken);
            throw_attempt(attempt);
        }
        if (is_auth_failure(attempt.status)) {
            token.clear(); // revoked; a fresh one is created below if needed
            publish_token(state, token);
            errors.push_back("saved access token rejected: " + attempt.error);
        } else {
            errors.push_back("HTTP: " + attempt.error);
        }
        last_status = attempt.status;
    }

    bool auth_required = false;
    {
        auto attempt = http_attempt(env, method, uri, body, limits, "");
        if (attempt.ok) {
            const auto wants = firmware_wants_token(state, env, uri, attempt.body, limits);
            // Mode stays unknown while the firmware is unknown, so the next
            // request asks again.
            if (wants.has_value()) publish_mode(state, RciMode::kHttpPlain);
            if (wants.value_or(false)) {
                if (const auto error = provision_token(state, env, token)) {
                    Logger::instance().warn(
                        "Keenetic RCI: using tokenless access, which KeeneticOS 5.2 is phasing "
                        "out: {}",
                        *error);
                } else {
                    // Prove the new token on the same request before relying on it.
                    const auto check = http_attempt(env, method, uri, body, limits, token);
                    if (check.ok) {
                        publish_mode(state, RciMode::kHttpToken);
                    } else if (is_auth_failure(check.status)) {
                        Logger::instance().warn("Keenetic RCI: {}",
                                                reject_fresh_token(state, token, check));
                    } else {
                        // No verdict on the token (timeout, other error): keep
                        // it and look again on the next request.
                        publish_mode(state, RciMode::kUnknown);
                    }
                }
            }
            return std::move(attempt.body);
        }
        if (attempt.status != 0 && !is_auth_failure(attempt.status)) {
            publish_mode(state, RciMode::kHttpPlain);
            throw_attempt(attempt);
        }
        auth_required = is_auth_failure(attempt.status);
        errors.push_back("HTTP: " + attempt.error);
        if (attempt.status != 0) last_status = attempt.status;
    }

    if (auth_required) {
        if (const auto error = provision_token(state, env, token)) {
            errors.push_back(*error);
        } else {
            auto attempt = http_attempt(env, method, uri, body, limits, token);
            if (attempt.ok) {
                publish_mode(state, RciMode::kHttpToken);
                return std::move(attempt.body);
            }
            if (is_auth_failure(attempt.status)) {
                errors.push_back(reject_fresh_token(state, token, attempt));
            } else {
                errors.push_back("new access token: " + attempt.error);
            }
            if (attempt.status != 0) last_status = attempt.status;
        }
    }

    {
        auto attempt = scgi_attempt(env, method, uri, body, limits);
        if (attempt.ok) {
            publish_mode(state, RciMode::kScgi);
            if (previous != RciMode::kScgi) {
                Logger::instance().verbose("Keenetic RCI: HTTP refused ({}); using the SCGI socket",
                                           errors.empty() ? "no answer" : errors.back());
            }
            return std::move(attempt.body);
        }
        if (!attempt.unavailable) errors.push_back("SCGI socket: " + attempt.error);
    }

    std::string message;
    for (const auto& error : errors) {
        if (error.empty()) continue;
        if (!message.empty()) message += "; ";
        message += error;
    }
    throw HttpError(message.empty() ? "Keenetic RCI is unavailable" : message, last_status);
}

std::string rci_request(const std::string& method, const std::string& uri, const std::string& body,
                        const RciRequestLimits& limits) {
    RciState& state = rci_state();

    const Snapshot first = take_snapshot(state);
    if (auto answer = try_known_way(first, method, uri, body, limits)) return std::move(*answer);

    std::lock_guard<std::mutex> search_lock(state.search_mutex);
    // Another thread may have found a way in while this one waited.
    const Snapshot current = take_snapshot(state);
    if (current.mode != first.mode || current.token != first.token) {
        if (auto answer = try_known_way(current, method, uri, body, limits)) {
            return std::move(*answer);
        }
    }
    return search_way_in(state, method, uri, body, limits);
}

} // namespace

// ---------------------------------------------------------------- parsers

namespace rci_detail {

std::string build_scgi_request(const std::string& method, const std::string& uri,
                               const std::string& body) {
    // The set that NDM on KeeneticOS 5.1.9 accepts; without HTTP_HOST it answers
    // "400, X-Detail: 0xcc56, missing host header".  CONTENT_LENGTH must come
    // first (SCGI spec).
    std::vector<std::pair<std::string, std::string>> vars = {
        {"CONTENT_LENGTH", std::to_string(body.size())},
        {"SCGI", "1"},
        {"REQUEST_METHOD", method},
        {"REQUEST_URI", uri},
        {"NORMALIZED_URI", uri},
        {"HTTP_HOST", "127.0.0.1:79"},
        {"SERVER_NAME", "127.0.0.1"},
        {"SERVER_PORT", "79"},
        {"SERVER_PROTOCOL", "HTTP/1.1"},
        {"REMOTE_ADDR", "127.0.0.1"},
    };
    if (!body.empty()) vars.emplace_back("CONTENT_TYPE", "application/json");
    vars.emplace_back("AUTH_ALLOWED", "1");

    std::string headers;
    for (const auto& [name, value] : vars) {
        headers += name;
        headers.push_back('\0');
        headers += value;
        headers.push_back('\0');
    }
    return std::to_string(headers.size()) + ":" + headers + "," + body;
}

std::optional<ScgiResponse> parse_scgi_response(const std::string& raw, bool at_eof) {
    std::size_t header_end = raw.find("\r\n\r\n");
    std::size_t body_start = header_end == std::string::npos ? std::string::npos : header_end + 4;
    if (header_end == std::string::npos) {
        header_end = raw.find("\n\n");
        if (header_end == std::string::npos) return std::nullopt;
        body_start = header_end + 2;
    }

    ScgiResponse response;
    response.status = 200; // CGI default when no Status header is sent
    std::optional<std::size_t> content_length;
    std::size_t pos = 0;
    bool first = true;
    while (pos < header_end) {
        std::size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos || eol > header_end) eol = header_end;
        std::string line = raw.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;

        if (first && line.rfind("HTTP/", 0) == 0) {
            const auto space = line.find(' ');
            const auto code = space == std::string::npos ? std::nullopt
                                                         : parse_status_code(line.substr(space + 1));
            if (!code) return ScgiResponse{0, "malformed status line", {}};
            response.status = *code;
            first = false;
            continue;
        }
        first = false;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower_copy(trim_copy(line.substr(0, colon)));
        const std::string value = trim_copy(line.substr(colon + 1));
        if (name == "status") {
            const auto code = parse_status_code(value);
            if (!code) return ScgiResponse{0, "malformed Status header", {}};
            response.status = *code;
        } else if (name == "x-detail") {
            response.detail = value;
        } else if (name == "content-length") {
            try {
                std::size_t used = 0;
                const unsigned long long parsed = std::stoull(value, &used);
                if (used != value.size()) throw std::invalid_argument("trailing characters");
                content_length = static_cast<std::size_t>(parsed);
            } catch (const std::exception&) {
                return ScgiResponse{0, "malformed Content-Length", {}};
            }
        }
    }

    const std::size_t available = raw.size() - body_start;
    if (content_length) {
        if (available < *content_length) return std::nullopt;
        response.body = raw.substr(body_start, *content_length);
        return response;
    }
    if (!at_eof) return std::nullopt;
    response.body = raw.substr(body_start);
    return response;
}

std::optional<GeneratedToken> parse_generated_token(const std::string& ndmc_output) {
    std::istringstream stream(ndmc_output);
    std::string line;
    std::string id;
    bool after_value = false;
    std::optional<std::string> value;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (after_value) {
            if (line.empty()) continue;
            value = line;
            break;
        }
        if (line.rfind("id:", 0) == 0) {
            id = trim_copy(line.substr(3));
        } else if (line.rfind("value:", 0) == 0) {
            const std::string inline_value = trim_copy(line.substr(6));
            if (!inline_value.empty()) {
                value = inline_value;
                break;
            }
            after_value = true;
        }
    }
    const bool numeric_id = !id.empty() && id.find_first_not_of("0123456789") == std::string::npos;
    if (!value || !valid_token(*value) || !numeric_id) return std::nullopt;
    return GeneratedToken{id, *value};
}

std::vector<std::string> parse_token_ids(const std::string& ndmc_output, const std::string& name) {
    std::vector<std::string> ids;
    std::istringstream stream(ndmc_output);
    std::string line;
    std::string current_id;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (line.rfind("token:", 0) == 0) {
            current_id.clear();
        } else if (line.rfind("id:", 0) == 0) {
            current_id = trim_copy(line.substr(3));
            const bool numeric = !current_id.empty() &&
                                 current_id.find_first_not_of("0123456789") == std::string::npos;
            if (!numeric) current_id.clear();
        } else if (line.rfind("user-data:", 0) == 0) {
            if (!current_id.empty() && trim_copy(line.substr(10)) == name) ids.push_back(current_id);
        }
    }
    return ids;
}

} // namespace rci_detail

// ---------------------------------------------------------------- public API

std::string rci_get(const std::string& path, const RciRequestLimits& limits) {
    std::size_t start = 0;
    while (start < path.size() && path[start] == '/') ++start;
    return rci_request("GET", "/rci/" + path.substr(start), "", limits);
}

std::string rci_post(const std::string& json_body, const RciRequestLimits& limits) {
    return rci_request("POST", "/rci/", json_body, limits);
}

void configure_rci_token_file(const std::string& path, bool allow_create) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> search_lock(state.search_mutex);
    state.token_file = path;
    state.may_create_token = allow_create;
    std::ifstream input(path);
    std::string token;
    if (input && std::getline(input, token)) {
        token = trim_copy(token);
        if (valid_token(token)) {
            publish_token(state, token);
        } else if (!token.empty()) {
            Logger::instance().warn("Keenetic RCI: ignoring malformed access token in {}", path);
        }
    }
}

#ifdef KEEN_PBR3_TESTING
void set_rci_scgi_socket_path_for_tests(const std::string& path) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.scgi_socket = path;
}

void set_rci_http_base_for_tests(const std::string& base) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.http_base = base;
}

void set_rci_http_transport_for_tests(std::shared_ptr<HttpTransport> transport) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.transport = std::move(transport);
}

void set_rci_ndmc_for_tests(RciNdmcFn ndmc) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.ndmc = std::move(ndmc);
}

void reset_rci_test_state() {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> search_lock(state.search_mutex);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.may_create_token = false;
    state.mode = RciMode::kUnknown;
    state.token.clear();
    state.token_file.clear();
    state.provision_blocked = false;
    state.firmware_wants_token.reset();
    state.last_provision_attempt.reset();
    state.last_provision_error.clear();
    state.scgi_socket = kDefaultScgiSocket;
    state.http_base = kDefaultHttpBase;
    state.transport.reset();
    state.ndmc = run_ndmc;
}
#endif

} // namespace keen_pbr3
