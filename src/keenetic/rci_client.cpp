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

struct RciState {
    std::mutex mutex;
    RciMode mode{RciMode::kUnknown};
    std::string token;
    std::string token_file; // empty: this process does not create tokens
    bool provision_blocked{false};            // retrying in this run cannot help
    std::optional<bool> firmware_wants_token; // KeeneticOS 5.2+, decided once
    std::optional<std::chrono::steady_clock::time_point> last_provision_attempt;
    std::string last_provision_error;

    std::string scgi_socket{kDefaultScgiSocket};
    std::string http_base{kDefaultHttpBase};
    std::shared_ptr<HttpTransport> transport;
    RciNdmcFn ndmc{run_ndmc};
};

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

Attempt http_attempt(RciState& state, const std::string& method, const std::string& uri,
                     const std::string& body, const RciRequestLimits& limits,
                     const std::string& token) {
    HttpTransportRequest request;
    request.url = state.http_base + uri;
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
        if (!state.transport) state.transport = default_http_transport();
        auto response = state.transport->perform(request);
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

Attempt scgi_attempt(RciState& state, const std::string& method, const std::string& uri,
                     const std::string& body, const RciRequestLimits& limits) {
    Attempt attempt;
    try {
        auto response = scgi_request(state.scgi_socket, method, uri, body, limits);
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
    const std::string temporary = path + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error(errno_text(("cannot write " + temporary).c_str()));
    const std::string content = token + "\n";
    const bool written = ::write(fd, content.data(), content.size()) ==
                         static_cast<ssize_t>(content.size());
    const bool synced = ::fsync(fd) == 0;
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
void remove_stale_tokens(RciState& state, const std::string& keep_id) {
    std::vector<std::string> tried;
    for (int round = 0; round < 8; ++round) {
        const auto listing = state.ndmc({"ndmc", "-c", "show authentication token"});
        std::string stale;
        for (const auto& id : rci_detail::parse_token_ids(listing.output, kTokenName)) {
            if (id != keep_id && std::find(tried.begin(), tried.end(), id) == tried.end()) {
                stale = id;
                break;
            }
        }
        if (stale.empty()) return;
        tried.push_back(stale);
        (void)state.ndmc({"ndmc", "-c", "authentication token delete " + stale});
    }
}

// Create a token and drop the keen-pbr tokens made before it.  The new token
// is saved before anything is deleted, so a working one always exists.
// Returns an error message, or no value on success.
std::optional<std::string> provision_token(RciState& state) {
    if (state.token_file.empty()) {
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

    const auto generated = state.ndmc(
        {"ndmc", "-c", std::string("authentication token generate ") + kTokenName});
    const auto token = rci_detail::parse_generated_token(generated.output);
    if (!token) return describe_generate_failure(state, generated);

    state.token = token->value;
    try {
        save_token(state.token_file, state.token);
    } catch (const std::exception& e) {
        // The token still works for this run.
        Logger::instance().warn("Keenetic RCI: cannot save the access token: {}", e.what());
    }
    remove_stale_tokens(state, token->id);
    Logger::instance().info("Keenetic RCI: created access token \"{}\" for user admin", kTokenName);
    state.last_provision_error.clear();
    return std::nullopt;
}

// A token NDM refuses right after creating it will not get better by making
// more of them; stop for this run.
std::string reject_fresh_token(RciState& state, const Attempt& attempt) {
    state.token.clear();
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
// Decided once per run.
bool firmware_wants_token(RciState& state, const std::string& uri, const std::string& body,
                          const RciRequestLimits& limits) {
    if (state.token_file.empty()) return false;
    if (!state.firmware_wants_token) {
        if (uri == "/rci/show/version") {
            state.firmware_wants_token = release_is_at_least_5_2(body);
        } else {
            const auto version = http_attempt(state, "GET", "/rci/show/version", "", limits, "");
            state.firmware_wants_token = version.ok && release_is_at_least_5_2(version.body);
        }
    }
    return *state.firmware_wants_token;
}

// ---------------------------------------------------------------- request

[[noreturn]] void throw_attempt(const Attempt& attempt) {
    throw HttpError(attempt.error, attempt.status);
}

std::string rci_request(const std::string& method, const std::string& uri, const std::string& body,
                        const RciRequestLimits& limits) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);

    // Reuse the way in that worked last time.  HTTP errors other than
    // 401/403 are answers to the request itself and need no new search.
    switch (state.mode) {
        case RciMode::kHttpToken:
        case RciMode::kHttpPlain: {
            const bool with_token = state.mode == RciMode::kHttpToken;
            auto attempt = http_attempt(state, method, uri, body, limits,
                                        with_token ? state.token : std::string());
            if (attempt.ok) return std::move(attempt.body);
            if (attempt.status != 0 && !is_auth_failure(attempt.status)) throw_attempt(attempt);
            break;
        }
        case RciMode::kScgi: {
            auto attempt = scgi_attempt(state, method, uri, body, limits);
            if (attempt.ok) return std::move(attempt.body);
            break;
        }
        case RciMode::kUnknown:
            break;
    }
    const RciMode previous = state.mode;
    state.mode = RciMode::kUnknown;

    std::vector<std::string> errors;
    long last_status = 0;

    if (!state.token.empty()) {
        auto attempt = http_attempt(state, method, uri, body, limits, state.token);
        if (attempt.ok) {
            state.mode = RciMode::kHttpToken;
            return std::move(attempt.body);
        }
        if (attempt.status != 0 && !is_auth_failure(attempt.status)) {
            state.mode = RciMode::kHttpToken;
            throw_attempt(attempt);
        }
        if (is_auth_failure(attempt.status)) {
            state.token.clear(); // revoked; a fresh one is created below if needed
            errors.push_back("saved access token rejected: " + attempt.error);
        } else {
            errors.push_back("HTTP: " + attempt.error);
        }
        last_status = attempt.status;
    }

    bool auth_required = false;
    {
        auto attempt = http_attempt(state, method, uri, body, limits, "");
        if (attempt.ok) {
            state.mode = RciMode::kHttpPlain;
            if (firmware_wants_token(state, uri, attempt.body, limits)) {
                if (const auto error = provision_token(state)) {
                    Logger::instance().warn(
                        "Keenetic RCI: using tokenless access, which KeeneticOS 5.2 is phasing "
                        "out: {}",
                        *error);
                } else {
                    // Prove the new token on the same request before relying on it.
                    const auto check = http_attempt(state, method, uri, body, limits, state.token);
                    if (check.ok) {
                        state.mode = RciMode::kHttpToken;
                    } else {
                        Logger::instance().warn("Keenetic RCI: {}", reject_fresh_token(state, check));
                    }
                }
            }
            return std::move(attempt.body);
        }
        if (attempt.status != 0 && !is_auth_failure(attempt.status)) {
            state.mode = RciMode::kHttpPlain;
            throw_attempt(attempt);
        }
        auth_required = is_auth_failure(attempt.status);
        errors.push_back("HTTP: " + attempt.error);
        if (attempt.status != 0) last_status = attempt.status;
    }

    if (auth_required) {
        if (const auto error = provision_token(state)) {
            errors.push_back(*error);
        } else {
            auto attempt = http_attempt(state, method, uri, body, limits, state.token);
            if (attempt.ok) {
                state.mode = RciMode::kHttpToken;
                return std::move(attempt.body);
            }
            if (is_auth_failure(attempt.status)) {
                errors.push_back(reject_fresh_token(state, attempt));
            } else {
                errors.push_back("new access token: " + attempt.error);
            }
            if (attempt.status != 0) last_status = attempt.status;
        }
    }

    {
        auto attempt = scgi_attempt(state, method, uri, body, limits);
        if (attempt.ok) {
            state.mode = RciMode::kScgi;
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

void configure_rci_token_file(const std::string& path) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.token_file = path;
    std::ifstream input(path);
    std::string token;
    if (input && std::getline(input, token)) {
        token = trim_copy(token);
        if (valid_token(token)) {
            state.token = token;
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
    std::lock_guard<std::mutex> lock(state.mutex);
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
