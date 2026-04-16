#include "daemon.hpp"

#include <nlohmann/json.hpp>
#include <sys/epoll.h>
#include <unistd.h>

#include "../dns/dns_probe_server.hpp"
#include "../log/logger.hpp"
#ifdef WITH_API
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

void Daemon::handle_dns_probe_query_event(const DnsProbeEvent& event) {
#ifdef WITH_API
    if (dns_test_broadcaster_) {
        nlohmann::json payload = {
            {"type", "DNS"},
            {"domain", event.domain},
            {"source_ip", event.source_ip},
            {"ecs", event.ecs.has_value() ? nlohmann::json(*event.ecs) : nlohmann::json(nullptr)},
        };
        dns_test_broadcaster_->publish(payload.dump());
    }
#else
    (void)event;
#endif
}

void Daemon::handle_dns_probe_udp_events(uint32_t events) {
    if ((events & EPOLLIN) && dns_probe_server_) {
        dns_probe_server_->handle_udp_readable();
    }
}

void Daemon::handle_dns_probe_tcp_client_events(int client_fd, uint32_t client_events) {
    if (!dns_probe_server_) {
        remove_fd(client_fd);
        close(client_fd);
        return;
    }

    bool keep_alive = false;
    if (client_events & EPOLLIN) {
        keep_alive = dns_probe_server_->handle_tcp_client_readable(client_fd);
    }
    if (client_events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        keep_alive = false;
    }

    if (!keep_alive) {
        dns_probe_server_->remove_tcp_client(client_fd);
        remove_fd(client_fd);
        close(client_fd);
    }
}

void Daemon::handle_dns_probe_tcp_listener_events(uint32_t events) {
    if (!(events & EPOLLIN) || !dns_probe_server_) {
        return;
    }

    for (int client_fd : dns_probe_server_->accept_tcp_clients()) {
        add_fd(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
               [this, client_fd](uint32_t client_events) {
                   handle_dns_probe_tcp_client_events(client_fd, client_events);
               });
    }
}

void Daemon::handle_dns_probe_tcp_timer_events(uint32_t events) {
    if (!(events & EPOLLIN) || !dns_probe_server_) {
        return;
    }
    for (int client_fd : dns_probe_server_->handle_tcp_idle_timeout()) {
        dns_probe_server_->remove_tcp_client(client_fd);
        remove_fd(client_fd);
        close(client_fd);
    }
}

void Daemon::setup_dns_probe() {
    teardown_dns_probe();

    if (!config_.dns || !config_.dns->dns_test_server.has_value()) {
        return;
    }

    const auto& test_cfg = *config_.dns->dns_test_server;
    const std::string* answer_ip = test_cfg.answer_ipv4 ? &*test_cfg.answer_ipv4 : nullptr;
    auto settings = parse_dns_probe_server_settings(test_cfg.listen, answer_ip);

    dns_probe_server_ = std::make_unique<DnsProbeServer>(
        settings,
        [this](const DnsProbeEvent& event) {
            handle_dns_probe_query_event(event);
        });

    add_fd(dns_probe_server_->udp_fd(), EPOLLIN, [this](uint32_t events) {
        handle_dns_probe_udp_events(events);
    });

    add_fd(dns_probe_server_->tcp_fd(), EPOLLIN, [this](uint32_t events) {
        handle_dns_probe_tcp_listener_events(events);
    });

    add_fd(dns_probe_server_->tcp_idle_timer_fd(), EPOLLIN, [this](uint32_t events) {
        handle_dns_probe_tcp_timer_events(events);
    });

    Logger::instance().info("DNS test server listening on {}", settings.listen);
}

void Daemon::teardown_dns_probe() {
    if (!dns_probe_server_) {
        return;
    }

    for (int fd : dns_probe_server_->all_fds()) {
        remove_fd(fd);
    }
    dns_probe_server_.reset();
}

} // namespace keen_pbr3
