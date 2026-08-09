#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/cmd/test_routing.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

std::filesystem::path make_temp_dir() {
    char path_template[] = "/tmp/keen-pbr-test-routing-XXXXXX";
    const char* created = mkdtemp(path_template);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::filesystem::path(created);
}

bool udp_socket_available() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

void push_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

size_t find_question_end(const uint8_t* packet, size_t packet_len) {
    size_t offset = 12;
    while (offset < packet_len) {
        const uint8_t len = packet[offset++];
        if (len == 0) {
            break;
        }
        offset += len;
    }
    if (offset + 4 > packet_len) {
        throw std::runtime_error("truncated DNS question");
    }
    return offset + 4;
}

uint16_t read_qtype(const uint8_t* packet, size_t packet_len) {
    const size_t end = find_question_end(packet, packet_len);
    return static_cast<uint16_t>((packet[end - 4] << 8) | packet[end - 3]);
}

std::vector<uint8_t> build_dns_response(const uint8_t* request,
                                        size_t request_len,
                                        const std::vector<std::string>& ipv4_answers,
                                        const std::vector<std::string>& ipv6_answers) {
    std::vector<uint8_t> packet;
    packet.reserve(512);

    const size_t question_end = find_question_end(request, request_len);
    const uint16_t qtype = read_qtype(request, request_len);
    const uint16_t answer_count =
        static_cast<uint16_t>((qtype == 1 ? ipv4_answers.size() : 0) +
                              (qtype == 28 ? ipv6_answers.size() : 0));

    push_u16(packet, static_cast<uint16_t>((request[0] << 8) | request[1]));
    push_u16(packet, 0x8180);
    push_u16(packet, 0x0001);
    push_u16(packet, answer_count);
    push_u16(packet, 0x0000);
    push_u16(packet, 0x0000);
    packet.insert(packet.end(), request + 12, request + question_end);

    const auto append_answer = [&packet](uint16_t type, const std::string& ip) {
        push_u16(packet, 0xC00C);
        push_u16(packet, type);
        push_u16(packet, 0x0001);
        push_u32(packet, 0);

        if (type == 1) {
            in_addr addr {};
            if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
                throw std::runtime_error("invalid IPv4 answer");
            }
            push_u16(packet, 4);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
            packet.insert(packet.end(), bytes, bytes + 4);
        } else {
            in6_addr addr {};
            if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) {
                throw std::runtime_error("invalid IPv6 answer");
            }
            push_u16(packet, 16);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
            packet.insert(packet.end(), bytes, bytes + 16);
        }
    };

    if (qtype == 1) {
        for (const auto& ip : ipv4_answers) {
            append_answer(1, ip);
        }
    } else if (qtype == 28) {
        for (const auto& ip : ipv6_answers) {
            append_answer(28, ip);
        }
    }

    return packet;
}

class TestDnsServer {
public:
    TestDnsServer(std::vector<std::string> ipv4_answers,
                  std::vector<std::string> ipv6_answers)
        : ipv4_answers_(std::move(ipv4_answers))
        , ipv6_answers_(std::move(ipv6_answers)) {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            throw std::runtime_error("socket() failed");
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(socket_fd_);
            throw std::runtime_error("bind() failed");
        }

        socklen_t len = sizeof(addr);
        if (getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            close(socket_fd_);
            throw std::runtime_error("getsockname() failed");
        }
        port_ = ntohs(addr.sin_port);

        server_thread_ = std::thread([this]() { serve(); });
    }

    ~TestDnsServer() {
        stop_ = true;
        if (socket_fd_ >= 0) {
            sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            (void)sendto(socket_fd_, "", 0, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

    std::string address() const {
        return "127.0.0.1:" + std::to_string(port_);
    }

private:
    void serve() {
        while (!stop_) {
            uint8_t buffer[512] = {};
            sockaddr_in client_addr {};
            socklen_t client_len = sizeof(client_addr);
            const ssize_t received = recvfrom(socket_fd_,
                                              buffer,
                                              sizeof(buffer),
                                              0,
                                              reinterpret_cast<sockaddr*>(&client_addr),
                                              &client_len);
            if (received <= 0) {
                continue;
            }
            if (stop_) {
                break;
            }

            auto response = build_dns_response(buffer,
                                               static_cast<size_t>(received),
                                               ipv4_answers_,
                                               ipv6_answers_);
            (void)sendto(socket_fd_,
                         response.data(),
                         response.size(),
                         0,
                         reinterpret_cast<const sockaddr*>(&client_addr),
                         client_len);
        }
    }

    int socket_fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> stop_{false};
    std::vector<std::string> ipv4_answers_;
    std::vector<std::string> ipv6_answers_;
    std::thread server_thread_;
};

Config build_test_config() {
    Config config;
    config.lists = std::map<std::string, ListConfig>{};
    config.dns = DnsConfig{};
    return config;
}

class ScopedPathOverride {
public:
    explicit ScopedPathOverride(const std::string& value) {
        if (const char* current = std::getenv("PATH")) {
            previous_ = current;
        }
        (void)setenv("PATH", value.c_str(), 1);
    }

    ~ScopedPathOverride() {
        if (previous_.has_value()) {
            (void)setenv("PATH", previous_->c_str(), 1);
        } else {
            (void)unsetenv("PATH");
        }
    }

private:
    std::optional<std::string> previous_;
};

void write_executable(const std::filesystem::path& path, const std::string& contents) {
    {
        std::ofstream output(path);
        output << contents;
    }
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
}

} // namespace

TEST_CASE("compute_test_routing resolves domain through configured system resolver") {
    if (!udp_socket_available()) {
        DOCTEST_INFO("UDP sockets unavailable in current environment");
        return;
    }

    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    TestDnsServer server({"10.0.0.53"}, {"2001:db8::53"});

    Config config = build_test_config();
    api::SystemResolver system_resolver;
    system_resolver.address = server.address();
    config.dns->system_resolver = system_resolver;

    const auto list_path = temp_dir / "resolved-ip-list.txt";
    {
        std::ofstream list(list_path);
        list << "10.0.0.53/32\n";
    }
    const auto domain_list_path = temp_dir / "domain-list.txt";
    {
        std::ofstream list(domain_list_path);
        list << "www.example.com\n";
    }
    ListConfig ip_list;
    ip_list.file = list_path.string();
    ListConfig domain_list;
    domain_list.file = domain_list_path.string();
    config.lists = std::map<std::string, ListConfig>{
        {"resolved_ips", ip_list}, {"domains", domain_list}};
    RouteRule ip_rule;
    ip_rule.outbound = "vpn";
    ip_rule.list = std::vector<std::string>{"resolved_ips"};
    RouteRule domain_rule;
    domain_rule.outbound = "vpn";
    domain_rule.list = std::vector<std::string>{"domains"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{ip_rule, domain_rule};
    config.route = route;

    const auto result = compute_test_routing(config, cache, "www.example.com");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips == std::vector<std::string>{"10.0.0.53", "2001:db8::53"});
    REQUIRE(result.entries.size() == 2);
    CHECK(result.entries[0].ip == "10.0.0.53");
    CHECK(result.entries[1].ip == "2001:db8::53");
    CHECK_FALSE(result.dns_error.has_value());
    REQUIRE(result.rule_diagnostics.size() == 2);
    const auto& ip_diagnostic = result.rule_diagnostics[0];
    CHECK_FALSE(ip_diagnostic.target_in_lists);
    REQUIRE(ip_diagnostic.ip_rows.size() == 2);
    CHECK(ip_diagnostic.ip_rows[0].in_lists);
    REQUIRE(ip_diagnostic.ip_rows[0].list_match.has_value());
    CHECK(ip_diagnostic.ip_rows[0].list_match->list_name == "resolved_ips");
    CHECK(ip_diagnostic.ip_rows[0].list_match->via == "10.0.0.53");
    CHECK_FALSE(ip_diagnostic.ip_rows[1].in_lists);
    CHECK_FALSE(ip_diagnostic.ip_rows[1].list_match.has_value());

    const auto& domain_diagnostic = result.rule_diagnostics[1];
    CHECK(domain_diagnostic.target_in_lists);
    REQUIRE(domain_diagnostic.ip_rows.size() == 2);
    for (const auto& ip_row : domain_diagnostic.ip_rows) {
        CHECK(ip_row.in_lists);
        REQUIRE(ip_row.list_match.has_value());
        CHECK(ip_row.list_match->list_name == "domains");
        CHECK(ip_row.list_match->via == "www.example.com");
    }

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing falls back to resolv.conf when system resolver is absent") {
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    Config config = build_test_config();

    const auto result = compute_test_routing(config, cache, "example.invalid");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips.empty());
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().ip == "(no IPs resolved)");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing includes route rule conditions in diagnostics") {
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    Config config = build_test_config();
    RouteRule rule;
    rule.outbound = "vpn";
    rule.list = std::vector<std::string>{"work", "media"};
    rule.proto = "tcp";
    rule.src_addr = "192.168.1.0/24";
    rule.dest_addr = "10.0.0.0/8";
    rule.src_port = "1024-65535";
    rule.dest_port = "443";

    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    const auto result = compute_test_routing(config, cache, "8.8.8.8");

    REQUIRE(result.rule_diagnostics.size() == 1);
    const auto& diagnostic_rule = result.rule_diagnostics.front().rule;
    CHECK(diagnostic_rule.outbound == "vpn");
    REQUIRE(diagnostic_rule.list.has_value());
    CHECK(*diagnostic_rule.list == std::vector<std::string>{"work", "media"});
    CHECK(diagnostic_rule.proto == "tcp");
    CHECK(diagnostic_rule.src_addr == "192.168.1.0/24");
    CHECK(diagnostic_rule.dest_addr == "10.0.0.0/8");
    CHECK(diagnostic_rule.src_port == "1024-65535");
    CHECK(diagnostic_rule.dest_port == "443");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing uses realized iptables generation set names") {
    const auto temp_dir = make_temp_dir();
    const auto bin_dir = temp_dir / "bin";
    const auto invocation_log = temp_dir / "ipset-invocations.txt";
    std::filesystem::create_directories(bin_dir);

    write_executable(bin_dir / "iptables", "#!/bin/sh\nexit 0\n");
    write_executable(
        bin_dir / "ipset",
        "#!/bin/sh\n"
        "echo test >> " + invocation_log.string() + "\n"
        "if [ \"$1\" = test ] && [ \"$2\" = kpbr4S_remote ] && "
        "[ \"$3\" = 203.0.113.10 ]; then\n"
        "  exit 0\n"
        "fi\n"
        "exit 1\n");
    ScopedPathOverride path_override(bin_dir.string() + ":/usr/bin:/bin");

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }

    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    Config config = build_test_config();
    ListConfig list;
    list.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", list}};

    DaemonConfig daemon;
    daemon.firewall_backend = api::DaemonConfigFirewallBackend::IPTABLES;
    config.daemon = daemon;

    Outbound outbound;
    outbound.tag = "vpn";
    outbound.type = OutboundType::TABLE;
    outbound.table = 100;
    config.outbounds = std::vector<Outbound>{outbound};

    RouteRule rule;
    rule.outbound = "vpn";
    rule.list = std::vector<std::string>{"remote"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    RuleState realized;
    realized.rule_index = 0;
    realized.list_names = {"remote"};
    realized.set_names = {"kpbr4S_remote"};
    realized.outbound_tag = "vpn";
    realized.action_type = RuleActionType::Mark;
    const std::vector<RuleState> realized_rules{realized};

    const auto result =
        compute_test_routing(config, cache, "203.0.113.10", &realized_rules);

    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().expected_outbound == "vpn");
    CHECK(result.entries.front().actual_outbound == "vpn");
    CHECK(result.entries.front().ok);
    REQUIRE(result.rule_diagnostics.size() == 1);
    REQUIRE(result.rule_diagnostics.front().ip_rows.size() == 1);
    CHECK(result.rule_diagnostics.front().ip_rows.front().in_lists);
    REQUIRE(result.rule_diagnostics.front().ip_rows.front().list_match.has_value());
    CHECK(result.rule_diagnostics.front().ip_rows.front().list_match->list_name == "remote");
    REQUIRE(result.rule_diagnostics.front().ip_rows.front().in_ipset.has_value());
    CHECK(*result.rule_diagnostics.front().ip_rows.front().in_ipset);

    std::ifstream invocations(invocation_log);
    const std::string invocation_contents{
        std::istreambuf_iterator<char>(invocations),
        std::istreambuf_iterator<char>()};
    CHECK(invocation_contents == "test\n");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("daemon test-routing response is rendered as a human-readable table") {
    const nlohmann::json response = {
        {"ok", true},
        {"result",
         {{"target", "example.com"},
          {"resolved_ips", {"203.0.113.10"}},
          {"warnings", nlohmann::json::array()},
          {"dns_error", nullptr},
          {"entries",
           {{{"ip", "203.0.113.10"},
             {"expected_outbound", "vpn"},
             {"actual_outbound", "vpn"},
             {"ok", true},
             {"list_match", {{"list_name", "domains"}, {"via", "example.com"}}}}}}}}};

    std::ostringstream stdout_capture;
    std::ostringstream stderr_capture;
    auto* previous_stdout = std::cout.rdbuf(stdout_capture.rdbuf());
    auto* previous_stderr = std::cerr.rdbuf(stderr_capture.rdbuf());
    const int exit_code = run_test_routing_command(response);
    std::cout.rdbuf(previous_stdout);
    std::cerr.rdbuf(previous_stderr);

    CHECK(exit_code == 0);
    CHECK(stderr_capture.str().empty());
    CHECK(stdout_capture.str().find("Target: example.com") != std::string::npos);
    CHECK(stdout_capture.str().find("Expected Outbound") != std::string::npos);
    CHECK(stdout_capture.str().find("domains (via example.com)") != std::string::npos);
    CHECK(stdout_capture.str().find("{\"") == std::string::npos);
}
