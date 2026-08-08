#ifdef WITH_API

#include "server.hpp"

#include "../crash/crash_diagnostics.hpp"
#include "../log/logger.hpp"
#include "../log/trace.hpp"
#include "../util/traced_mutex.hpp"
#include "../auth/password.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

class CrashAwareTaskQueue final : public httplib::TaskQueue {
public:
    CrashAwareTaskQueue()
        : delegate_(CPPHTTPLIB_THREAD_POOL_COUNT, CPPHTTPLIB_THREAD_POOL_MAX_COUNT) {}

    bool enqueue(std::function<void()> fn) override {
        return delegate_.enqueue([fn = std::move(fn)]() mutable {
            if (!crash_diagnostics::install_for_current_thread()) {
                std::abort();
            }
            fn();
        });
    }

    void shutdown() override { delegate_.shutdown(); }

private:
    httplib::ThreadPool delegate_;
};

std::string make_error_json(const std::string& message) {
    return nlohmann::json{{"error", message}}.dump();
}

std::string get_mime_type_for_path(const std::filesystem::path& path) {
    static const std::unordered_map<std::string, std::string> kMimeByExtension{
        {".css", "text/css"},
        {".csv", "text/csv"},
        {".gif", "image/gif"},
        {".htm", "text/html"},
        {".html", "text/html"},
        {".ico", "image/x-icon"},
        {".jpeg", "image/jpeg"},
        {".jpg", "image/jpeg"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".map", "application/json"},
        {".mjs", "application/javascript"},
        {".png", "image/png"},
        {".svg", "image/svg+xml"},
        {".txt", "text/plain"},
        {".wasm", "application/wasm"},
        {".webp", "image/webp"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".xml", "application/xml"},
    };

    const auto ext = path.extension().string();
    const auto it = kMimeByExtension.find(ext);
    if (it != kMimeByExtension.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

bool read_file(const std::filesystem::path& path, std::string& output) {
    constexpr std::uintmax_t kMaxStaticFileSize = std::uintmax_t{32} * 1024U * 1024U; // 32 MiB

    std::error_code ec;
    auto file_size = std::filesystem::file_size(path, ec);
    if (ec || file_size > kMaxStaticFileSize) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    output.resize(static_cast<size_t>(file_size));
    file.read(output.data(), static_cast<std::streamsize>(file_size));
    return !file.bad();
}

bool serve_file_response(httplib::Response& res,
                         const std::filesystem::path& path,
                         const std::filesystem::path& mime_from_path,
                         bool gzip_encoded) {
    if (gzip_encoded) {
        res.set_file_content(path.string(), get_mime_type_for_path(mime_from_path));
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Vary", "Accept-Encoding");
        return true;
    }

    std::string body;
    if (!read_file(path, body)) {
        return false;
    }
    res.set_content(body, get_mime_type_for_path(mime_from_path));
    return true;
}

std::string trim_ascii(std::string value) {
    auto first = value.begin();
    while (first != value.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }

    auto last = value.end();
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }

    return std::string(first, last);
}

bool parse_accept_encoding_token(const std::string& token, std::string& encoding, double& q) {
    q = 1.0;
    const auto semicolon = token.find(';');
    encoding = trim_ascii(token.substr(0, semicolon));
    if (encoding.empty()) {
        return false;
    }

    std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (semicolon == std::string::npos) {
        return true;
    }

    size_t cursor = semicolon + 1;
    while (cursor < token.size()) {
        const auto next = token.find(';', cursor);
        const auto param = trim_ascii(token.substr(cursor, next == std::string::npos
                                                              ? std::string::npos
                                                              : next - cursor));
        const auto equals = param.find('=');
        if (equals != std::string::npos) {
            auto name = trim_ascii(param.substr(0, equals));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (name == "q") {
                char* end = nullptr;
                const auto value = trim_ascii(param.substr(equals + 1));
                q = std::strtod(value.c_str(), &end);
                return end != value.c_str() && q >= 0.0;
            }
        }

        if (next == std::string::npos) {
            break;
        }
        cursor = next + 1;
    }

    return true;
}

bool request_accepts_gzip(const httplib::Request& req) {
    const auto header_count = req.get_header_value_count("Accept-Encoding");
    if (header_count == 0) {
        return false;
    }

    std::optional<double> gzip_q;
    std::optional<double> wildcard_q;
    for (size_t header_index = 0; header_index < header_count; ++header_index) {
        const auto header = req.get_header_value("Accept-Encoding", "", header_index);
        size_t cursor = 0;
        while (cursor <= header.size()) {
            const auto next = header.find(',', cursor);
            const auto token = header.substr(cursor, next == std::string::npos
                                                         ? std::string::npos
                                                         : next - cursor);
            std::string encoding;
            double q = 0.0;
            if (parse_accept_encoding_token(token, encoding, q)) {
                if (encoding == "gzip") {
                    gzip_q = q;
                } else if (encoding == "*") {
                    wildcard_q = q;
                }
            }

            if (next == std::string::npos) {
                break;
            }
            cursor = next + 1;
        }
    }

    return gzip_q.value_or(wildcard_q.value_or(0.0)) > 0.0;
}

std::int64_t request_duration_ms(std::chrono::steady_clock::time_point started_at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
}

void log_request_start(const httplib::Request& req, const char* kind) {
    Logger::instance().trace("http_request_start",
                             "method={} path={} kind={}",
                             req.method,
                             req.path,
                             kind);
}

void log_request_end(const httplib::Request& req,
                     const char* kind,
                     int status,
                     std::chrono::steady_clock::time_point started_at) {
    Logger::instance().trace("http_request_end",
                             "method={} path={} kind={} status={} duration_ms={}",
                             req.method,
                             req.path,
                             kind,
                             status,
                             request_duration_ms(started_at));
}

void log_request_error(const httplib::Request& req,
                       const char* kind,
                       const std::string& error,
                       std::chrono::steady_clock::time_point started_at) {
    Logger::instance().trace("http_request_error",
                             "method={} path={} kind={} duration_ms={} error={}",
                             req.method,
                             req.path,
                             kind,
                             request_duration_ms(started_at),
                             error);
}

bool path_starts_with(const std::filesystem::path& path,
                      const std::filesystem::path& prefix) {
    auto path_it = path.begin();
    auto prefix_it = prefix.begin();

    for (; prefix_it != prefix.end(); ++prefix_it, ++path_it) {
        if (path_it == path.end() || *path_it != *prefix_it) {
            return false;
        }
    }

    return true;
}

bool is_safe_static_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }

    return true;
}

bool resolve_static_file_under_root(const std::filesystem::path& root,
                                    const std::filesystem::path& path,
                                    std::filesystem::path& resolved) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }

    ec.clear();
    resolved = std::filesystem::canonical(path, ec);
    if (ec || !path_starts_with(resolved, root)) {
        return false;
    }

    return true;
}

bool is_regular_file_or_gzip(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        return true;
    }

    ec.clear();
    auto gzip_path = path;
    gzip_path += ".gz";
    return std::filesystem::is_regular_file(gzip_path, ec);
}

std::optional<std::string> decode_basic_base64(std::string_view input) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    unsigned value = 0;
    int bits = -8;
    for (const unsigned char ch : input) {
        if (ch == '=') break;
        const auto position = alphabet.find(static_cast<char>(ch));
        if (position == std::string_view::npos) return std::nullopt;
        value = (value << 6U) | static_cast<unsigned>(position);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((value >> bits) & 0xffU));
            bits -= 8;
        }
    }
    return output;
}

} // namespace

struct ApiServer::Impl {
    httplib::Server server;
    std::string host;
    int port;
    std::thread listen_thread;
    std::atomic<bool> is_listening{false};
    std::atomic<bool> listen_failed{false};
    std::atomic<bool> listen_finished{false};
    TracedMutex state_mutex;
    std::condition_variable_any startup_cv;
    std::string listen_error_message;
    bool auth_enabled{false};
    std::string password_hash;
    std::vector<std::string> allowed_origins;
    std::mutex auth_mutex;
    std::string session_token_hash;
    std::chrono::system_clock::time_point session_expires{};
    std::string basic_cache_hash;
    std::chrono::steady_clock::time_point basic_cache_expires{};
    unsigned failed_logins{0};
    std::chrono::steady_clock::time_point failure_window{};
};

ApiServerLimits api_server_limits(const ApiConfig& config) {
    ApiServerLimits limits;
    limits.max_request_body_bytes = static_cast<std::size_t>(
        config.max_request_body_bytes.value_or(1024 * 1024));
    limits.read_timeout_seconds = static_cast<int>(config.read_timeout_seconds.value_or(15));
    limits.write_timeout_seconds = static_cast<int>(config.write_timeout_seconds.value_or(15));
    limits.keep_alive_timeout_seconds = static_cast<int>(
        config.keep_alive_timeout_seconds.value_or(20));
    return limits;
}

ApiServer::ApiServer(const ApiConfig& config) : impl_(std::make_unique<Impl>()) {
    impl_->server.new_task_queue = [] { return new CrashAwareTaskQueue(); };
    const ApiServerLimits limits = api_server_limits(config);
    impl_->server.set_payload_max_length(limits.max_request_body_bytes);
    impl_->server.set_read_timeout(limits.read_timeout_seconds);
    impl_->server.set_write_timeout(limits.write_timeout_seconds);
    impl_->server.set_keep_alive_timeout(limits.keep_alive_timeout_seconds);
    if (config.authentication) {
        impl_->auth_enabled = config.authentication->enabled.value_or(false);
        impl_->password_hash = config.authentication->password_hash.value_or("");
    }
    if (config.cors) {
        impl_->allowed_origins = config.cors->allowed_origins.value_or(
            std::vector<std::string>{});
    }

    auto bearer_authenticated = [this](const httplib::Request& req) {
        const auto header = req.get_header_value("Authorization");
        constexpr std::string_view prefix = "Bearer ";
        if (header.compare(0, prefix.size(), prefix.data(), prefix.size()) != 0) return false;
        const auto hash = auth::blake2b_hex(header.substr(prefix.size()));
        const std::lock_guard lock(impl_->auth_mutex);
        if (std::chrono::system_clock::now() >= impl_->session_expires) {
            impl_->session_token_hash.clear();
            return false;
        }
        return !impl_->session_token_hash.empty() &&
               auth::constant_time_equal(hash, impl_->session_token_hash);
    };
    auto request_authenticated = [this, bearer_authenticated](const httplib::Request& req) {
        if (bearer_authenticated(req)) return true;
        const auto header = req.get_header_value("Authorization");
        constexpr std::string_view prefix = "Basic ";
        if (header.compare(0, prefix.size(), prefix.data(), prefix.size()) != 0) return false;
        const auto header_hash = auth::blake2b_hex(header);
        {
            const std::lock_guard lock(impl_->auth_mutex);
            if (std::chrono::steady_clock::now() < impl_->basic_cache_expires &&
                auth::constant_time_equal(header_hash, impl_->basic_cache_hash)) {
                return true;
            }
        }
        const auto decoded_value = decode_basic_base64(header.substr(prefix.size()));
        if (!decoded_value) return false;
        const auto& decoded = *decoded_value;
        const auto colon = decoded.find(':');
        const bool valid = colon != std::string::npos && decoded.substr(0, colon) == "admin" &&
                           auth::verify_password(decoded.substr(colon + 1), impl_->password_hash);
        if (valid) {
            const std::lock_guard lock(impl_->auth_mutex);
            impl_->basic_cache_hash = header_hash;
            impl_->basic_cache_expires = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        }
        return valid;
    };
    auto apply_cors = [this](const httplib::Request& req, httplib::Response& res) {
        const auto origin = req.get_header_value("Origin");
        if (origin.empty()) return true;
        const auto host = req.get_header_value("Host");
        const bool same_origin = origin == "http://" + host;
        bool allowed = same_origin;
        if (impl_->auth_enabled && !allowed) {
            const bool extension = origin.rfind("chrome-extension://", 0) == 0 ||
                                   origin.rfind("moz-extension://", 0) == 0;
            allowed = extension || std::find(impl_->allowed_origins.begin(),
                                              impl_->allowed_origins.end(), origin) !=
                                     impl_->allowed_origins.end();
        }
        if (!allowed) return false;
        if (!same_origin) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, Accept");
            res.set_header("Access-Control-Max-Age", "600");
        }
        return true;
    };

    impl_->server.set_pre_routing_handler(
        [this, request_authenticated, apply_cors](const httplib::Request& req,
                                                  httplib::Response& res) {
            if (!apply_cors(req, res)) {
                res.status = 403;
                res.set_content(make_error_json("origin not allowed"), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.method == "OPTIONS") {
                res.status = 204;
                return httplib::Server::HandlerResponse::Handled;
            }
            const bool api = req.path == "/api" || req.path.rfind("/api/", 0) == 0;
            const bool public_auth = req.path == "/api/auth/status" ||
                                     req.path == "/api/auth/login";
            if (api && impl_->auth_enabled && !public_auth && !request_authenticated(req)) {
                res.status = 401;
                res.set_header("WWW-Authenticate", "Basic realm=\"keen-pbr\"");
                res.set_content(make_error_json("authentication required"), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    impl_->server.Get("/api/auth/status", [this, bearer_authenticated](const httplib::Request& req,
                                                                        httplib::Response& res) {
        res.set_content(nlohmann::json{{"enabled", impl_->auth_enabled},
                                       {"authenticated", !impl_->auth_enabled ||
                                                             bearer_authenticated(req)}}.dump(),
                        "application/json");
    });
    impl_->server.Post("/api/auth/login", [this](const httplib::Request& req,
                                                   httplib::Response& res) {
        if (!impl_->auth_enabled) {
            res.status = 409;
            res.set_content(make_error_json("authentication is disabled"), "application/json");
            return;
        }
        try {
            {
                const std::lock_guard lock(impl_->auth_mutex);
                const auto now = std::chrono::steady_clock::now();
                if (now - impl_->failure_window >= std::chrono::minutes(1)) {
                    impl_->failure_window = now;
                    impl_->failed_logins = 0;
                }
                if (impl_->failed_logins >= 5) {
                    res.status = 429;
                    res.set_header("Retry-After", "60");
                    res.set_content(make_error_json("too many authentication attempts"), "application/json");
                    return;
                }
            }
            const auto body = nlohmann::json::parse(req.body);
            if (!body.contains("password") || !body["password"].is_string() ||
                !auth::verify_password(body["password"].get<std::string>(), impl_->password_hash)) {
                const std::lock_guard lock(impl_->auth_mutex);
                ++impl_->failed_logins;
                res.status = 401;
                res.set_content(make_error_json("invalid credentials"), "application/json");
                return;
            }
            const auto token = auth::random_token();
            const auto expires = std::chrono::system_clock::now() + std::chrono::hours(24);
            {
                const std::lock_guard lock(impl_->auth_mutex);
                // There is deliberately only one UI session. A new login invalidates the old token.
                impl_->session_token_hash = auth::blake2b_hex(token);
                impl_->session_expires = expires;
                impl_->failed_logins = 0;
            }
            const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                expires.time_since_epoch()).count();
            res.set_content(nlohmann::json{{"token", token}, {"expires_at", epoch}}.dump(),
                            "application/json");
        } catch (const nlohmann::json::exception&) {
            res.status = 400;
            res.set_content(make_error_json("invalid request"), "application/json");
        }
    });
    impl_->server.Post("/api/auth/logout", [this](const httplib::Request&,
                                                    httplib::Response& res) {
        const std::lock_guard lock(impl_->auth_mutex);
        impl_->session_token_hash.clear();
        res.status = 204;
    });
    // Parse "host:port" from config.listen
    const std::string listen = config.listen.value_or("0.0.0.0:12121");
    auto colon = listen.rfind(':');
    if (colon == std::string::npos) {
        throw ApiError("Invalid listen address: " + listen +
                       " (expected host:port)");
    }

    impl_->host = listen.substr(0, colon);
    std::string port_str = listen.substr(colon + 1);

    try {
        impl_->port = std::stoi(port_str);
    } catch (const std::exception&) {
        throw ApiError("Invalid port in listen address: " + port_str);
    }

    if (impl_->port <= 0 || impl_->port > 65535) {
        throw ApiError("Port out of range: " + port_str);
    }
}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::get(const std::string& path, RouteHandler handler) {
    impl_->server.Get(path, [h = std::move(handler)](const httplib::Request& req,
                                                      httplib::Response& res) {
        const auto trace_id = allocate_trace_id();
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        log_request_start(req, "api");
        try {
            std::string body = h();
            res.set_content(body, "application/json");
            log_request_end(req, "api", res.status == 0 ? 200 : res.status, started_at);
        } catch (const ApiAccepted& accepted) {
            res.status = 202;
            res.set_content(accepted.body(), "application/json");
            log_request_end(req, "api", res.status, started_at);
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        }
    });
}

void ApiServer::post(const std::string& path, RouteHandler handler) {
    impl_->server.Post(path, [h = std::move(handler)](const httplib::Request& req,
                                                       httplib::Response& res) {
        const auto trace_id = allocate_trace_id();
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        log_request_start(req, "api");
        try {
            std::string body = h();
            res.set_content(body, "application/json");
            log_request_end(req, "api", res.status == 0 ? 200 : res.status, started_at);
        } catch (const ApiAccepted& accepted) {
            res.status = 202;
            res.set_content(accepted.body(), "application/json");
            log_request_end(req, "api", res.status, started_at);
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        }
    });
}

void ApiServer::post(const std::string& path, BodyRouteHandler handler) {
    impl_->server.Post(path, [h = std::move(handler)](const httplib::Request& req,
                                                       httplib::Response& res) {
        const auto trace_id = allocate_trace_id();
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        log_request_start(req, "api");
        try {
            std::string result = h(req.body);
            res.set_content(result, "application/json");
            log_request_end(req, "api", res.status == 0 ? 200 : res.status, started_at);
        } catch (const ApiAccepted& accepted) {
            res.status = 202;
            res.set_content(accepted.body(), "application/json");
            log_request_end(req, "api", res.status, started_at);
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
            log_request_error(req, "api", e.what(), started_at);
            log_request_end(req, "api", res.status, started_at);
        }
    });
}

void ApiServer::get_stream(const std::string& path, StreamRouteHandler handler) {
    impl_->server.Get(path, [h = std::move(handler)](const httplib::Request& req,
                                                      httplib::Response& res) {
        const auto trace_id = allocate_trace_id();
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        log_request_start(req, "stream");
        try {
            h(req, res);
            log_request_end(req, "stream", res.status == 0 ? 200 : res.status, started_at);
        } catch (const std::exception& e) {
            if (!res.status) {
                res.status = 500;
            }
            if (res.body.empty()) {
                res.set_content(make_error_json(e.what()), "application/json");
            }
            log_request_error(req, "stream", e.what(), started_at);
            log_request_end(req, "stream", res.status, started_at);
        }
    });
}

bool ApiServer::register_static_root(const std::string& frontend_root) {
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::path(frontend_root), ec);
    if (ec || !fs::is_directory(root)) {
        return false;
    }

    impl_->server.Get(R"(/(.*))", [root](const httplib::Request& req,
                                          httplib::Response& res) {
        const auto trace_id = allocate_trace_id();
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        log_request_start(req, "static");

        auto finish = [&req, &res, started_at]() {
            log_request_end(req, "static", res.status == 0 ? 200 : res.status, started_at);
        };

        const bool accepts_gzip = request_accepts_gzip(req);

        auto serve_index = [&res, &root, accepts_gzip]() -> bool {
            const fs::path index_path = root / "index.html";
            auto index_gzip_path = index_path;
            index_gzip_path += ".gz";

            fs::path resolved_index_gzip;
            if (accepts_gzip &&
                resolve_static_file_under_root(root, index_gzip_path, resolved_index_gzip)) {
                return serve_file_response(res, resolved_index_gzip, index_path, true);
            }

            fs::path resolved_index;
            if (resolve_static_file_under_root(root, index_path, resolved_index)) {
                return serve_file_response(res, resolved_index, index_path, false);
            }

            return false;
        };

        const bool is_api_route = req.path == "/api" || req.path.rfind("/api/", 0) == 0;
        if (is_api_route) {
            res.status = 404;
            res.set_content(make_error_json("not found"), "application/json");
            finish();
            return;
        }

        const fs::path relative = (req.path == "/"
                                       ? fs::path("index.html")
                                       : fs::path(req.path).relative_path())
                                      .lexically_normal();

        if (!is_safe_static_relative_path(relative)) {
            res.status = 400;
            res.set_content(make_error_json("invalid path"), "application/json");
            finish();
            return;
        }

        std::error_code ec;
        const fs::path requested = fs::absolute(root / relative, ec).lexically_normal();
        if (ec || !path_starts_with(requested, root)) {
            res.status = 400;
            res.set_content(make_error_json("invalid path"), "application/json");
            finish();
            return;
        }

        fs::path requested_gzip = requested;
        requested_gzip += ".gz";

        fs::path resolved_gzip;
        if (accepts_gzip && resolve_static_file_under_root(root, requested_gzip, resolved_gzip)) {
            if (serve_file_response(res, resolved_gzip, requested, true)) {
                finish();
                return;
            }
            res.status = 500;
            res.set_content(make_error_json("failed to read static file"), "application/json");
            finish();
            return;
        }

        fs::path resolved_requested;
        if (resolve_static_file_under_root(root, requested, resolved_requested)) {
            if (serve_file_response(res, resolved_requested, requested, false)) {
                finish();
                return;
            }
            res.status = 500;
            res.set_content(make_error_json("failed to read static file"), "application/json");
            finish();
            return;
        }

        if (serve_index()) {
            finish();
            return;
        }

        res.status = 404;
        res.set_content(make_error_json("not found"), "application/json");
        finish();
    });

    return true;
}

void ApiServer::start() {
    if (impl_->is_listening.load(std::memory_order_acquire) && impl_->server.is_running()) {
        return;
    }

    impl_->listen_failed.store(false, std::memory_order_release);
    impl_->listen_finished.store(false, std::memory_order_release);
    {
        KPBR_LOCK_GUARD(impl_->state_mutex);
        impl_->listen_error_message.clear();
    }

    impl_->listen_thread = std::thread([this]() {
        if (!crash_diagnostics::install_for_current_thread()) {
            std::abort();
        }
        std::string error_message;
        bool listen_ok = false;

        try {
            listen_ok = impl_->server.listen(impl_->host, impl_->port);
            if (!listen_ok) {
                error_message = "listen() returned false";
                const int listen_errno = errno;
                if (listen_errno != 0) {
                    error_message += ": ";
                    error_message += std::strerror(listen_errno);
                }
                impl_->listen_failed.store(true, std::memory_order_release);
            }
        } catch (const std::exception& e) {
            error_message = e.what();
            impl_->listen_failed.store(true, std::memory_order_release);
        } catch (...) {
            error_message = "Unknown listen thread error";
            impl_->listen_failed.store(true, std::memory_order_release);
        }

        impl_->is_listening.store(listen_ok, std::memory_order_release);
        impl_->listen_finished.store(true, std::memory_order_release);
        {
            KPBR_UNIQUE_LOCK(lock, impl_->state_mutex);
            if (!error_message.empty()) {
                impl_->listen_error_message = std::move(error_message);
            }
        }
        impl_->startup_cv.notify_all();
    });

    {
        constexpr auto startup_timeout = std::chrono::seconds(3);
        constexpr auto poll_interval = std::chrono::milliseconds(50);
        const auto deadline = std::chrono::steady_clock::now() + startup_timeout;
        KPBR_UNIQUE_LOCK(lock, impl_->state_mutex);
        while (!impl_->server.is_running() &&
               !impl_->listen_failed.load(std::memory_order_acquire) &&
               !impl_->listen_finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            impl_->startup_cv.wait_for(lock, poll_interval);
        }
    }

    if (impl_->server.is_running()) {
        impl_->is_listening.store(true, std::memory_order_release);
        return;
    }

    std::string diagnostic;
    {
        KPBR_LOCK_GUARD(impl_->state_mutex);
        diagnostic = impl_->listen_error_message;
    }
    if (diagnostic.empty()) {
        diagnostic = impl_->listen_finished.load(std::memory_order_acquire)
            ? "listen thread exited before server became running"
            : "startup timed out after 3s";
    }

    stop();
    throw ApiError("Failed to start API server on " + impl_->host + ":" +
                   std::to_string(impl_->port) + " (" + diagnostic + ")");
}

void ApiServer::stop() {
    if (impl_ && impl_->server.is_running()) {
        impl_->server.stop();
    }
    if (impl_ && impl_->listen_thread.joinable()) {
        impl_->listen_thread.join();
    }
    if (impl_) {
        impl_->is_listening.store(false, std::memory_order_release);
    }
}

bool ApiServer::listening() const {
    return impl_->is_listening.load(std::memory_order_acquire) && impl_->server.is_running();
}

} // namespace keen_pbr3

#endif // WITH_API
