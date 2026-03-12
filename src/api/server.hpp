#pragma once

#ifdef WITH_API

#include "../config/config.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace httplib {
class Request;
class Response;
}

namespace keen_pbr3 {

class Daemon;

class ApiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// HTTP REST API server using cpp-httplib.
// Runs in a background thread and integrates with the Daemon event loop
// for shutdown coordination.
class ApiServer {
public:
    // Construct with listen address from ApiConfig.
    // Does not start listening until start() is called.
    explicit ApiServer(const ApiConfig& config);
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

    // Start listening in a background thread.
    void start();

    // Stop the server and join the background thread.
    void stop();

    // Returns true if the server is currently listening.
    bool listening() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3

#endif // WITH_API
