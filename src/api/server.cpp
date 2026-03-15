#ifdef WITH_API

#include "server.hpp"

#include <httplib.h>
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
    bool is_listening{false};
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
    if (impl_->is_listening) {
        return;
    }

    impl_->listen_thread = std::thread([this]() {
        impl_->is_listening = impl_->server.listen(impl_->host, impl_->port);
    });

    // Wait briefly for the server to start listening
    while (!impl_->server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    impl_->is_listening = true;
}

void ApiServer::stop() {
    if (impl_ && impl_->server.is_running()) {
        impl_->server.stop();
    }
    if (impl_ && impl_->listen_thread.joinable()) {
        impl_->listen_thread.join();
    }
    if (impl_) {
        impl_->is_listening = false;
    }
}

bool ApiServer::listening() const {
    return impl_->is_listening && impl_->server.is_running();
}

} // namespace keen_pbr3

#endif // WITH_API
