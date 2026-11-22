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
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace keen_pbr3 {

namespace {

constexpr const char* kDefaultHttpBase = "http://127.0.0.1:79";
constexpr const char* kTokenHeader = "X-Ndma-Tkn";
// user-data of the tokens this service creates; stale ones are deleted by it.
constexpr const char* kTokenName = "keen-pbr";
// A failed token creation is not retried sooner than this.
constexpr auto kProvisionRetryInterval = std::chrono::minutes(10);

enum class RciMode { kUnknown, kHttpPlain, kHttpToken };

// Whether this firmware has RCI access tokens, learned from `ndmc ... generate`
// (and from a token being accepted).
enum class TokenSupport { kUnknown, kSupported, kUnsupported };

using RciClock = std::function<std::chrono::steady_clock::time_point()>;

std::chrono::steady_clock::time_point steady_now() { return std::chrono::steady_clock::now(); }

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
    std::string http_base;
    std::shared_ptr<HttpTransport> transport;
    RciNdmcFn ndmc;
    RciClock now;
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
    std::string http_base{kDefaultHttpBase};     // [shared]
    std::shared_ptr<HttpTransport> transport;    // [shared]
    RciNdmcFn ndmc{run_ndmc};                    // [shared]
    RciClock now{steady_now};                    // [shared]
    // Tokenless access is in use because no token could be created; when this
    // time has passed the next request looks for a way in again.
    std::optional<std::chrono::steady_clock::time_point> plain_recheck_at; // [shared]

    std::string token_file;                   // [search] where the token is kept
    bool may_create_token{false};             // [search] only the service creates tokens
    bool provision_blocked{false};            // [search] retrying in this run cannot help
    TokenSupport token_support{TokenSupport::kUnknown}; // [search]
    std::optional<std::chrono::steady_clock::time_point> last_provision_attempt; // [search]
    std::string last_provision_error;         // [search]
    std::string last_logged_provision_error;  // [search] one warning per distinct error
};

struct Snapshot {
    RciMode mode{RciMode::kUnknown};
    std::string token;
    std::optional<std::chrono::steady_clock::time_point> plain_recheck_at;
    RciEnv env;
};

Snapshot take_snapshot(RciState& state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.transport) state.transport = default_http_transport();
    return {state.mode, state.token, state.plain_recheck_at,
            {state.http_base, state.transport, state.ndmc, state.now}};
}

// Setting a mode also sets (or clears) the recheck time that belongs to it.
void publish_mode(RciState& state, RciMode mode,
                  std::optional<std::chrono::steady_clock::time_point> plain_recheck_at = std::nullopt) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.mode = mode;
    state.plain_recheck_at = plain_recheck_at;
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

std::string errno_text(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

// ---------------------------------------------------------------- attempts

struct Attempt {
    bool ok{false};
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

// Only a firmware without tokens and a missing ndmc block creating tokens for
// the rest of the run.  Anything else (the admin user lacking the Web
// configurator permission, a failing ndmc) can be fixed while running, so it is
// retried after kProvisionRetryInterval.
std::string describe_generate_failure(RciState& state, const RciNdmcResult& result) {
    const std::string output = lower_copy(result.output);
    if (output.find("argument parse error") != std::string::npos ||
        output.find("no such command") != std::string::npos) {
        state.token_support = TokenSupport::kUnsupported;
        return block_provisioning(state, "this KeeneticOS version has no RCI access tokens");
    }
    if (result.exit_code == 127 || result.exit_code < 0) {
        return block_provisioning(state, "ndmc is not available");
    }
    if (output.find("invalid username") != std::string::npos) {
        state.last_provision_error =
            "the admin user needs the \"Web configurator\" permission to create an RCI "
            "access token";
        return state.last_provision_error;
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
    const auto now = env.now();
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

    state.token_support = TokenSupport::kSupported;
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
    state.last_logged_provision_error.clear();
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

// ---------------------------------------------------------------- request

[[noreturn]] void throw_attempt(const Attempt& attempt) {
    throw HttpError(attempt.error, attempt.status);
}

// A 401/403 the known way in answered, handed to the search so it does not
// send the same request again to get the same refusal.  An empty token means
// the refused request was tokenless.
struct Rejection {
    std::string token;
    Attempt attempt;
};

// One request on the way in that worked last time.  Returns the body on
// success.  Throws for an answer that needs no new search (an HTTP error other
// than 401/403, or no HTTP answer at all: a timeout says nothing about the
// token or the mode).  Returns no value when a new search is warranted, and
// then sends nothing (tokenless access whose recheck time has come) or has
// recorded the refusal in `rejected`.
std::optional<std::string> try_known_way(const Snapshot& snap, const std::string& method,
                                         const std::string& uri, const std::string& body,
                                         const RciRequestLimits& limits,
                                         std::optional<Rejection>& rejected) {
    switch (snap.mode) {
        case RciMode::kHttpToken:
        case RciMode::kHttpPlain: {
            if (snap.mode == RciMode::kHttpPlain && snap.plain_recheck_at &&
                snap.env.now() >= *snap.plain_recheck_at) {
                return std::nullopt; // time to see whether a token can be created now
            }
            const std::string token =
                snap.mode == RciMode::kHttpToken ? snap.token : std::string();
            auto attempt = http_attempt(snap.env, method, uri, body, limits, token);
            if (attempt.ok) return std::move(attempt.body);
            if (!is_auth_failure(attempt.status)) throw_attempt(attempt);
            rejected = Rejection{token, std::move(attempt)};
            return std::nullopt;
        }
        case RciMode::kUnknown:
            return std::nullopt;
    }
    return std::nullopt;
}

std::string join_errors(const std::vector<std::string>& errors) {
    std::string message;
    for (const auto& error : errors) {
        if (error.empty()) continue;
        if (!message.empty()) message += "; ";
        message += error;
    }
    return message.empty() ? "Keenetic RCI is unavailable" : message;
}

// Find a way in.  Runs under search_mutex, so one thread at a time.
//
//   1. a token we hold: a verdict (answer, timeout, other error) is final; only
//      a refusal (401/403) moves on, forgetting the token in memory.
//   2. the service creates a token (KeeneticOS 5.2+ accepts one even before it
//      enforces them) and sends the request with it once.
//   3. tokenless, for firmware without tokens and while none can be created.
//
// A request is never sent twice with an answer in between: after a timeout or
// another error the request may have run, so it is not repeated (a POST must
// not execute twice).  A refusal means it did not run.
std::string search_way_in(RciState& state, const std::string& method, const std::string& uri,
                          const std::string& body, const RciRequestLimits& limits,
                          const std::optional<Rejection>& rejected) {
    Snapshot snap = take_snapshot(state);
    const RciEnv& env = snap.env;
    std::string token = snap.token;
    publish_mode(state, RciMode::kUnknown);

    std::vector<std::string> errors;
    long last_status = 0;

    // 1. The token we hold.
    if (!token.empty()) {
        auto attempt = (rejected && rejected->token == token)
                           ? rejected->attempt
                           : http_attempt(env, method, uri, body, limits, token);
        if (attempt.ok) {
            state.token_support = TokenSupport::kSupported;
            publish_mode(state, RciMode::kHttpToken);
            return std::move(attempt.body);
        }
        if (!is_auth_failure(attempt.status)) {
            // A timeout or another error says nothing about the token: keep it
            // and do not look for another way in.
            publish_mode(state, RciMode::kHttpToken);
            throw_attempt(attempt);
        }
        // Revoked.  Forgotten in memory only; the file is replaced when a new
        // token is written.
        token.clear();
        publish_token(state, token);
        errors.push_back("saved access token rejected: " + attempt.error);
        last_status = attempt.status;
    }

    // 2. A new token, created by the service.
    std::optional<std::string> provision_error;
    if (state.may_create_token && state.token_support != TokenSupport::kUnsupported) {
        provision_error = provision_token(state, env, token);
        if (provision_error) {
            errors.push_back(*provision_error);
        } else {
            const auto attempt = http_attempt(env, method, uri, body, limits, token);
            if (attempt.ok) {
                state.token_support = TokenSupport::kSupported;
                publish_mode(state, RciMode::kHttpToken);
                return attempt.body;
            }
            if (!is_auth_failure(attempt.status)) {
                // No verdict on the token: keep it, and the request is not
                // sent again (it may have run).
                publish_mode(state, RciMode::kHttpToken);
                throw_attempt(attempt);
            }
            errors.push_back(reject_fresh_token(state, token, attempt));
            last_status = attempt.status;
        }
    }

    // 3. Without a token.
    {
        auto attempt = (rejected && rejected->token.empty())
                           ? rejected->attempt
                           : http_attempt(env, method, uri, body, limits, "");
        if (attempt.ok) {
            std::optional<std::chrono::steady_clock::time_point> recheck;
            if (state.may_create_token && state.token_support != TokenSupport::kUnsupported) {
                if (!state.provision_blocked && state.last_provision_attempt) {
                    recheck = *state.last_provision_attempt + kProvisionRetryInterval;
                    if (*recheck <= env.now()) recheck = env.now() + kProvisionRetryInterval;
                }
                const std::string& reason =
                    provision_error ? *provision_error : state.last_provision_error;
                if (!reason.empty() && reason != state.last_logged_provision_error) {
                    state.last_logged_provision_error = reason;
                    Logger::instance().warn(
                        "Keenetic RCI: using tokenless access, which KeeneticOS 5.2 is phasing "
                        "out: {}",
                        reason);
                }
            }
            publish_mode(state, RciMode::kHttpPlain, recheck);
            return std::move(attempt.body);
        }
        if (attempt.status != 0 && !is_auth_failure(attempt.status)) {
            publish_mode(state, RciMode::kHttpPlain);
            throw_attempt(attempt);
        }
        errors.push_back("HTTP: " + attempt.error);
        if (attempt.status != 0) last_status = attempt.status;
        if (is_auth_failure(attempt.status) && !state.may_create_token) {
            errors.push_back("an RCI access token is required; the keen-pbr service creates it");
        }
    }

    throw HttpError(join_errors(errors), last_status);
}

std::string rci_request(const std::string& method, const std::string& uri, const std::string& body,
                        const RciRequestLimits& limits) {
    RciState& state = rci_state();

    std::optional<Rejection> rejected;
    const Snapshot first = take_snapshot(state);
    if (auto answer = try_known_way(first, method, uri, body, limits, rejected)) {
        return std::move(*answer);
    }

    std::lock_guard<std::mutex> search_lock(state.search_mutex);
    // Another thread may have changed the way in while this one waited.
    const Snapshot current = take_snapshot(state);
    if (current.mode != first.mode || current.token != first.token ||
        current.plain_recheck_at != first.plain_recheck_at) {
        rejected.reset();
        if (auto answer = try_known_way(current, method, uri, body, limits, rejected)) {
            return std::move(*answer);
        }
    }
    return search_way_in(state, method, uri, body, limits, rejected);
}

} // namespace

// ---------------------------------------------------------------- parsers

namespace rci_detail {

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

std::vector<std::string> missing_netfilter_components(const std::string& components_json) {
    // A JSON string or array naming the installed components.  Anything else
    // (an error object, an empty answer) is a failure, not "all missing".
    const auto answer = nlohmann::json::parse(components_json);
    std::string text;
    if (answer.is_string()) {
        text = answer.get<std::string>();
    } else if (answer.is_array()) {
        for (const auto& item : answer) {
            if (item.is_string()) text += item.get<std::string>() + ' ';
        }
    }
    if (text.find_first_not_of(" \t\r\n,") == std::string::npos) {
        throw std::runtime_error("unexpected components answer");
    }
    std::vector<std::string> have;
    std::string word;
    for (const char c : text + ' ') {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
            word += c;
        } else if (!word.empty()) {
            have.push_back(std::move(word));
            word.clear();
        }
    }
    std::vector<std::string> missing;
    for (const char* component : {"opkg-kmod-netfilter", "opkg-kmod-netfilter-addons"}) {
        if (std::find(have.begin(), have.end(), component) == have.end()) {
            missing.emplace_back(component);
        }
    }
    return missing;
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

bool rci_answers(const RciRequestLimits& limits, std::string* note) {
    try {
        (void)rci_get("show/version", limits);
        return true;
    } catch (const HttpError& e) {
        // 401/403: RCI is up and wants an access token.
        if (is_auth_failure(e.status_code())) {
            if (note != nullptr) {
                *note = "RCI is up; it needs an access token, which the service creates";
            }
            return true;
        }
        if (note != nullptr) *note = e.what();
        return false;
    }
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

void set_rci_clock_for_tests(std::function<std::chrono::steady_clock::time_point()> now) {
    RciState& state = rci_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.now = now ? std::move(now) : RciClock(steady_now);
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
    state.token_support = TokenSupport::kUnknown;
    state.plain_recheck_at.reset();
    state.last_provision_attempt.reset();
    state.last_provision_error.clear();
    state.last_logged_provision_error.clear();
    state.now = steady_now;
    state.http_base = kDefaultHttpBase;
    state.transport.reset();
    state.ndmc = run_ndmc;
}
#endif

} // namespace keen_pbr3
