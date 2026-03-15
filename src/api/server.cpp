#ifdef WITH_API

#include "server.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <httplib.h>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

std::string make_error_json(const std::string& message) {
    return nlohmann::json{{"error", message}}.dump();
}

} // namespace

struct ApiServer::Impl {
    httplib::Server server;
    std::string host;
    int port;
    std::thread listen_thread;
    std::atomic<bool> is_listening{false};
    std::atomic<bool> listen_failed{false};
    std::mutex state_mutex;
    std::condition_variable startup_cv;
    bool startup_done{false};
    std::string listen_error_message;
};

ApiServer::ApiServer(const ApiConfig& config) : impl_(std::make_unique<Impl>()) {
    // Parse "host:port" from config.listen
    const std::string listen = config.listen.value_or("0.0.0.0:8080");
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
    impl_->server.Get(path, [h = std::move(handler)](const httplib::Request&,
                                                      httplib::Response& res) {
        try {
            std::string body = h();
            res.set_content(body, "application/json");
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
        }
    });
}

void ApiServer::post(const std::string& path, RouteHandler handler) {
    impl_->server.Post(path, [h = std::move(handler)](const httplib::Request&,
                                                       httplib::Response& res) {
        try {
            std::string body = h();
            res.set_content(body, "application/json");
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
        }
    });
}

void ApiServer::post(const std::string& path, BodyRouteHandler handler) {
    impl_->server.Post(path, [h = std::move(handler)](const httplib::Request& req,
                                                       httplib::Response& res) {
        try {
            std::string result = h(req.body);
            res.set_content(result, "application/json");
        } catch (const ApiError& e) {
            res.status = e.status();
            res.set_content(e.body().value_or(make_error_json(e.what())), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(make_error_json(e.what()), "application/json");
        }
    });
}

void ApiServer::get_stream(const std::string& path, StreamRouteHandler handler) {
    impl_->server.Get(path, [h = std::move(handler)](const httplib::Request& req,
                                                      httplib::Response& res) {
        try {
            h(req, res);
        } catch (const std::exception& e) {
            if (!res.status) {
                res.status = 500;
            }
            if (res.body.empty()) {
                res.set_content(make_error_json(e.what()), "application/json");
            }
        }
    });
}

void ApiServer::start() {
    if (impl_->is_listening.load(std::memory_order_acquire) && impl_->server.is_running()) {
        return;
    }

    impl_->listen_failed.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->startup_done = false;
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
        {
            std::lock_guard<std::mutex> lock(impl_->state_mutex);
            impl_->startup_done = true;
            if (!error_message.empty()) {
                impl_->listen_error_message = std::move(error_message);
            }
        }
        impl_->startup_cv.notify_all();
    });

    constexpr auto startup_timeout = std::chrono::seconds(3);
    std::unique_lock<std::mutex> lock(impl_->state_mutex);
    const bool startup_finished = impl_->startup_cv.wait_for(lock, startup_timeout, [this]() {
        return impl_->server.is_running() ||
               impl_->listen_failed.load(std::memory_order_acquire) ||
               impl_->startup_done;
    });
    std::string diagnostic = impl_->listen_error_message;
    lock.unlock();

    if (impl_->server.is_running()) {
        impl_->is_listening.store(true, std::memory_order_release);
        return;
    }
    if (diagnostic.empty()) {
        diagnostic = startup_finished
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
