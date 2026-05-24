#include <doctest/doctest.h>

#include "../src/daemon/list_service.hpp"
#include "../src/log/logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace keen_pbr3;

namespace {

std::filesystem::path make_temp_dir() {
    char path_template[] = "/tmp/keen-pbr-list-service-XXXXXX";
    const char* created = mkdtemp(path_template);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::filesystem::path(created);
}

class LoggerCapture {
public:
    LoggerCapture() : previous_level_(Logger::instance().level()) {
        Logger::instance().set_level(LogLevel::debug);
        Logger::instance().set_sink([this](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back(line);
        });
    }

    ~LoggerCapture() {
        Logger::instance().clear_sink();
        Logger::instance().set_level(previous_level_);
    }

    bool contains(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::any_of(lines_.begin(), lines_.end(), [&needle](const std::string& line) {
            return line.find(needle) != std::string::npos;
        });
    }

private:
    LogLevel previous_level_;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

struct CurlGlobalGuard {
    CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
};

struct HttpResponse {
    int status{200};
    std::string reason{"OK"};
    std::string body;
    std::vector<std::string> headers;
};

class TestHttpServer {
public:
    explicit TestHttpServer(std::map<std::string, HttpResponse> routes)
        : routes_(std::move(routes)) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("socket failed");
        }

        int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(listen_fd_);
            throw std::runtime_error("bind failed");
        }

        socklen_t len = sizeof(addr);
        if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            close(listen_fd_);
            throw std::runtime_error("getsockname failed");
        }
        port_ = ntohs(addr.sin_port);

        if (listen(listen_fd_, 8) < 0) {
            close(listen_fd_);
            throw std::runtime_error("listen failed");
        }

        worker_ = std::thread([this]() { serve(); });
    }

    ~TestHttpServer() {
        running_.store(false);
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

private:
    void serve() {
        while (running_.load()) {
            const int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (!running_.load()) {
                    break;
                }
                continue;
            }
            handle_client(client_fd);
            close(client_fd);
        }
    }

    void handle_client(int client_fd) {
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            request.append(buffer, static_cast<size_t>(n));
            if (request.size() > 8192) {
                break;
            }
        }

        std::string path = "/";
        const auto first_space = request.find(' ');
        if (first_space != std::string::npos) {
            const auto second_space = request.find(' ', first_space + 1);
            if (second_space != std::string::npos) {
                path = request.substr(first_space + 1, second_space - first_space - 1);
            }
        }

        HttpResponse response;
        auto it = routes_.find(path);
        if (it != routes_.end()) {
            response = it->second;
        } else {
            response.status = 404;
            response.reason = "Not Found";
        }

        std::ostringstream out;
        out << "HTTP/1.1 " << response.status << ' ' << response.reason << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Connection: close\r\n";
        for (const auto& header : response.headers) {
            out << header << "\r\n";
        }
        out << "\r\n" << response.body;
        const auto payload = out.str();
        (void)send(client_fd, payload.data(), payload.size(), 0);
    }

    std::map<std::string, HttpResponse> routes_;
    int listen_fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> running_{true};
    std::thread worker_;
};

} // namespace

TEST_CASE("select_remote_list_targets: refresh-all selects only URL-backed lists") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";

    ListConfig inline_only;
    inline_only.domains = std::vector<std::string>{"example.com"};

    config.lists = std::map<std::string, ListConfig>{
        {"inline", inline_only},
        {"local", local_file},
        {"remote", remote},
    };

    const auto selection = select_remote_list_targets(config, std::nullopt);

    CHECK(selection.ok());
    CHECK(selection.list_names.size() == 1);
    CHECK(selection.list_names.front() == "remote");
}

TEST_CASE("select_remote_list_targets: single URL-backed list is accepted") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto selection = select_remote_list_targets(config, std::string("remote"));

    CHECK(selection.ok());
    CHECK(selection.list_names == std::vector<std::string>{"remote"});
}

TEST_CASE("select_remote_list_targets: unknown list returns not found") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto selection = select_remote_list_targets(config, std::string("missing"));

    CHECK(selection.error == RemoteListTargetSelectionError::NotFound);
    CHECK(selection.list_names.empty());
}

TEST_CASE("select_remote_list_targets: non-URL-backed list returns not remote") {
    Config config;

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";
    config.lists = std::map<std::string, ListConfig>{{"local", local_file}};

    const auto selection = select_remote_list_targets(config, std::string("local"));

    CHECK(selection.error == RemoteListTargetSelectionError::NotRemote);
    CHECK(selection.list_names.empty());
}

TEST_CASE("should_reload_runtime_after_list_refresh: only relevant changes reload active runtime") {
    RemoteListsRefreshResult refresh_result;
    refresh_result.changed_lists = {"remote"};
    refresh_result.relevant_changed_lists = {"remote"};

    CHECK(should_reload_runtime_after_list_refresh(true, refresh_result));
    CHECK_FALSE(should_reload_runtime_after_list_refresh(false, refresh_result));

    refresh_result.relevant_changed_lists.clear();
    CHECK_FALSE(should_reload_runtime_after_list_refresh(true, refresh_result));
}

TEST_CASE("build_list_refresh_state_map: URL-backed lists expose last_updated metadata only") {
    const auto temp_dir = make_temp_dir();
    CacheManager cache_manager(temp_dir);
    cache_manager.ensure_dir();

    CacheMetadata metadata;
    metadata.download_time = "2026-04-05T12:34:56Z";
    cache_manager.save_metadata("remote", metadata);

    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";

    config.lists = std::map<std::string, ListConfig>{
        {"remote", remote},
        {"local", local_file},
    };

    const auto refresh_state = build_list_refresh_state_map(config, cache_manager);

    auto remote_it = refresh_state.find("remote");
    CHECK(remote_it != refresh_state.end());
    REQUIRE(remote_it->second.last_updated.has_value());
    CHECK(*remote_it->second.last_updated == "2026-04-05T12:34:56Z");
    CHECK(refresh_state.find("local") == refresh_state.end());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("refresh_remote_lists: failed HTTP list logs status and refresh continues") {
    CurlGlobalGuard curl_guard;
    TestHttpServer server({
        {"/bad.txt", HttpResponse{404, "Not Found", ""}},
        {"/ok.txt", HttpResponse{200, "OK", "example.com\n"}},
    });
    LoggerCapture logs;

    const auto temp_dir = make_temp_dir();
    ListService service(temp_dir);
    service.ensure_dir();

    ListConfig bad;
    bad.url = server.url("/bad.txt");
    ListConfig ok;
    ok.url = server.url("/ok.txt");

    Config config;
    config.lists = std::map<std::string, ListConfig>{
        {"bad", bad},
        {"ok", ok},
    };

    const auto result = service.refresh_remote_lists(config, OutboundMarkMap{});

    CHECK(result.failed_lists == std::vector<std::string>{"bad"});
    CHECK(result.changed_lists == std::vector<std::string>{"ok"});
    CHECK(service.cache_manager().has_cache("ok"));
    CHECK_FALSE(service.cache_manager().has_cache("bad"));
    CHECK(logs.contains("List 'bad': failed to refresh " + *bad.url + ": HTTP 404"));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("download_uncached: reports relevant changed lists for startup resolver reload") {
    CurlGlobalGuard curl_guard;
    TestHttpServer server({
        {"/route.txt", HttpResponse{200, "OK", "example.com\n"}},
        {"/unused.txt", HttpResponse{200, "OK", "unused.example\n"}},
    });

    const auto temp_dir = make_temp_dir();
    ListService service(temp_dir);
    service.ensure_dir();

    ListConfig route_list;
    route_list.url = server.url("/route.txt");
    ListConfig unused_list;
    unused_list.url = server.url("/unused.txt");

    Config config;
    config.lists = std::map<std::string, ListConfig>{
        {"route", route_list},
        {"unused", unused_list},
    };

    const std::set<std::string> relevant_lists{"route"};
    const auto result =
        service.download_uncached(config, OutboundMarkMap{}, &relevant_lists);

    CHECK(result.changed_lists == std::vector<std::string>{"route", "unused"});
    CHECK(result.relevant_changed_lists == std::vector<std::string>{"route"});

    const auto second_result =
        service.download_uncached(config, OutboundMarkMap{}, &relevant_lists);
    CHECK(second_result.changed_lists.empty());
    CHECK(second_result.relevant_changed_lists.empty());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("refresh_remote_lists: failed curl request logs clear transport error") {
    CurlGlobalGuard curl_guard;
    LoggerCapture logs;

    const auto temp_dir = make_temp_dir();
    ListService service(temp_dir);
    service.ensure_dir();

    ListConfig remote;
    remote.url = "http://127.0.0.1:1/missing.txt";

    Config config;
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto result = service.refresh_remote_lists(config, OutboundMarkMap{});

    CHECK(result.failed_lists == std::vector<std::string>{"remote"});
    CHECK(logs.contains("List 'remote': failed to refresh http://127.0.0.1:1/missing.txt:"));
    CHECK_FALSE(logs.contains("HTTP request failed:"));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("refresh_remote_lists: 304 not modified does not log a warning") {
    CurlGlobalGuard curl_guard;
    TestHttpServer server({
        {"/not-modified.txt", HttpResponse{304, "Not Modified", ""}},
    });
    LoggerCapture logs;

    const auto temp_dir = make_temp_dir();
    {
        CacheManager cache_manager(temp_dir);
        cache_manager.ensure_dir();
        CacheMetadata metadata;
        metadata.etag = "\"abc\"";
        cache_manager.save_metadata("remote", metadata);
    }

    ListService service(temp_dir);
    service.ensure_dir();

    ListConfig remote;
    remote.url = server.url("/not-modified.txt");

    Config config;
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto result = service.refresh_remote_lists(config, OutboundMarkMap{});

    CHECK(result.failed_lists.empty());
    CHECK(result.changed_lists.empty());
    CHECK_FALSE(logs.contains("failed to refresh"));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("collect_relevant_list_names: ignores disabled route and dns rules") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";
    config.lists = std::map<std::string, ListConfig>{
        {"route_disabled", remote},
        {"route_enabled", remote},
        {"dns_disabled", remote},
        {"dns_enabled", remote},
    };

    RouteRule route_disabled;
    route_disabled.enabled = false;
    route_disabled.list = std::vector<std::string>{"route_disabled"};
    route_disabled.outbound = "vpn";

    RouteRule route_enabled;
    route_enabled.list = std::vector<std::string>{"route_enabled"};
    route_enabled.outbound = "vpn";

    RouteConfig route_config;
    route_config.rules = std::vector<RouteRule>{route_disabled, route_enabled};
    config.route = route_config;

    DnsRule dns_disabled;
    dns_disabled.enabled = false;
    dns_disabled.list = std::vector<std::string>{"dns_disabled"};
    dns_disabled.server = "dns1";

    DnsRule dns_enabled;
    dns_enabled.list = std::vector<std::string>{"dns_enabled"};
    dns_enabled.server = "dns1";

    DnsConfig dns_config;
    dns_config.rules = std::vector<DnsRule>{dns_disabled, dns_enabled};
    config.dns = dns_config;

    const auto relevant_lists = collect_relevant_list_names(config);

    CHECK(relevant_lists.count("route_disabled") == 0);
    CHECK(relevant_lists.count("dns_disabled") == 0);
    CHECK(relevant_lists.count("route_enabled") == 1);
    CHECK(relevant_lists.count("dns_enabled") == 1);
}
