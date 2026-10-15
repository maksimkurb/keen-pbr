#pragma once

#ifdef WITH_API

#include "../config/config.hpp"

#include <functional>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace httplib {
class Request;
class Response;
}

namespace keen_pbr3 {

struct ApiServerLimits {
    std::size_t max_request_body_bytes{std::size_t{1024} * 1024U};
    int read_timeout_seconds{15};
    int write_timeout_seconds{15};
    int keep_alive_timeout_seconds{20};
};

ApiServerLimits api_server_limits(const ApiConfig& config);

class ApiError : public std::runtime_error {
public:
    explicit ApiError(std::string message,
                      int status = 500,
                      std::optional<std::string> body = std::nullopt)
        : std::runtime_error(std::move(message))
        , status_(status)
        , body_(std::move(body)) {}

    int status() const noexcept {
        return status_;
    }

    const std::optional<std::string>& body() const noexcept {
        return body_;
    }

private:
    int status_;
    std::optional<std::string> body_;
};

// A deliberate non-error response status for asynchronous endpoints.
class ApiAccepted final : public std::runtime_error {
public:
    explicit ApiAccepted(std::string body) : std::runtime_error("Accepted"), body_(std::move(body)) {}
    const std::string& body() const noexcept { return body_; }
private:
    std::string body_;
};

// HTTP REST API server using cpp-httplib.
// Runs in a background thread and integrates with the Daemon event loop
// for shutdown coordination.
class ApiServer {
public:
    // Construct with listen address from ApiConfig.
    // Does not start listening until start() is called.
    explicit ApiServer(const ApiConfig& config, std::string device_name = {});
    ~ApiServer();

    // Non-copyable, non-movable
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;
    ApiServer(ApiServer&&) = delete;
    ApiServer& operator=(ApiServer&&) = delete;

    // Register route handlers before calling start().
    using RouteHandler = std::function<std::string()>;
    using BodyRouteHandler = std::function<std::string(const std::string& body)>;
    using StreamRouteHandler = std::function<void(const httplib::Request&,
                                                  httplib::Response&)>;

    // Register a GET handler that returns a JSON string.
    void get(const std::string& path, RouteHandler handler);

    // Register a POST handler that returns a JSON string.
    void post(const std::string& path, RouteHandler handler);

    // Register a POST handler that receives the request body and returns a JSON string.
    void post(const std::string& path, BodyRouteHandler handler);

    // Register a GET handler that streams a non-JSON response.
    void get_stream(const std::string& path, StreamRouteHandler handler);

    // Register static file serving from frontend_root for non-/api routes.
    // Returns false if static serving could not be configured.
    bool register_static_root(const std::string& frontend_root);

    // Start listening in a background thread.
    void start();

    // Stop the server and join the background thread.
    void stop();

    // Returns true if the server is currently listening.
    bool listening() const;

    // True when API authentication is enabled in the applied settings.
    bool auth_enabled() const;

    // Predicate for a long-lived response opened by `request`: it stays true
    // while the credentials the stream was opened with remain valid (same
    // password, same UI session, not expired) and, for a stream opened without
    // authentication, while authentication stays disabled. Cheap enough to
    // call once per heartbeat; it never verifies a password.
    using StreamAuthorization = std::function<bool()>;
    StreamAuthorization stream_authorization(const httplib::Request& request);

    // Apply authentication, CORS, and WebUI identity settings without
    // restarting the listening socket. Auth changes revoke active sessions.
    void update_runtime_config(const ApiConfig& config, std::string device_name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3

#endif // WITH_API
