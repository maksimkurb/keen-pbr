#ifdef WITH_API

#include "server.hpp"

#include "../log/logger.hpp"
#include "../log/trace.hpp"
#include "../util/traced_mutex.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <atomic>
#include <cctype>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

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
    std::string body;
    if (!read_file(path, body)) {
        return false;
    }

    res.set_content(body, get_mime_type_for_path(mime_from_path));
    if (gzip_encoded) {
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Vary", "Accept-Encoding");
    }
    return true;
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
};

ApiServer::ApiServer(const ApiConfig& config) : impl_(std::make_unique<Impl>()) {
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

        auto serve_index = [&res, &root]() -> bool {
            const fs::path index_path = root / "index.html";
            auto index_gzip_path = index_path;
            index_gzip_path += ".gz";

            fs::path resolved_index_gzip;
            if (resolve_static_file_under_root(root, index_gzip_path, resolved_index_gzip)) {
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
        if (resolve_static_file_under_root(root, requested_gzip, resolved_gzip)) {
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
