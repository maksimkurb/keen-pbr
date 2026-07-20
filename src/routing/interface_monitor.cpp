#include "interface_monitor.hpp"

#include "../util/format_compat.hpp"

#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unordered_map>

#include <netlink/attr.h>
#include <netlink/errno.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>

namespace keen_pbr3 {

namespace {

constexpr int kLinkAttributeMax = IFLA_MAX + 1;

} // namespace

struct InterfaceMonitor::Impl {
    explicit Impl(InterfaceStateCallback callback)
        : callback(std::move(callback)) {}

    ~Impl() {
        close_socket();
    }

    static int on_nl_message(struct nl_msg* msg, void* arg) {
        auto* impl = static_cast<Impl*>(arg);
        if (!impl) {
            return NL_OK;
        }

        impl->handle_message(msg);
        return NL_OK;
    }

    void handle_message(struct nl_msg* msg) {
        if (!msg || !callback) {
            return;
        }

        struct nlmsghdr* hdr = nlmsg_hdr(msg);
        if (!hdr) {
            return;
        }

        if (hdr->nlmsg_type == RTM_NEWADDR || hdr->nlmsg_type == RTM_DELADDR) {
            auto* addr = static_cast<ifaddrmsg*>(nlmsg_data(hdr));
            if (!addr || (addr->ifa_family != AF_INET && addr->ifa_family != AF_INET6)) {
                return;
            }
            char name[IF_NAMESIZE] = {};
            if (if_indextoname(addr->ifa_index, name) != nullptr) {
                callback(Event{std::string(name), false, false});
            }
            return;
        }

        if (hdr->nlmsg_type != RTM_NEWLINK && hdr->nlmsg_type != RTM_DELLINK) {
            return;
        }

        auto* if_info = static_cast<ifinfomsg*>(nlmsg_data(hdr));
        if (!if_info) {
            return;
        }

        struct nlattr* attrs[kLinkAttributeMax] = {};
        const int parse_err = nlmsg_parse(hdr,
                                          sizeof(*if_info),
                                          attrs,
                                          IFLA_MAX,
                                          nullptr);
        if (parse_err < 0 || attrs[IFLA_IFNAME] == nullptr) {
            return;
        }

        const char* if_name_raw = static_cast<const char*>(nla_data(attrs[IFLA_IFNAME]));
        if (!if_name_raw || *if_name_raw == '\0') {
            return;
        }

        const std::string interface_name(if_name_raw);
        const bool is_up = (hdr->nlmsg_type == RTM_NEWLINK) && ((if_info->ifi_flags & IFF_UP) != 0);

        const auto previous = interface_state.find(interface_name);
        const bool administrative_state_changed =
            hdr->nlmsg_type == RTM_NEWLINK &&
            previous != interface_state.end() && previous->second != is_up;
        if (hdr->nlmsg_type == RTM_DELLINK) {
            interface_state.erase(interface_name);
        } else {
            interface_state[interface_name] = is_up;
        }
        callback(Event{interface_name, administrative_state_changed, is_up});
    }

    void close_socket() {
        if (!socket) {
            return;
        }
        nl_close(socket);
        nl_socket_free(socket);
        socket = nullptr;
    }

    void setup_socket() {
        close_socket();

        socket = nl_socket_alloc();
        if (!socket) {
            throw InterfaceMonitorError("Failed to allocate netlink socket for interface monitor");
        }

        int err = nl_connect(socket, NETLINK_ROUTE);
        if (err < 0) {
            close_socket();
            throw InterfaceMonitorError(
                format("Failed to connect interface monitor netlink socket: {}", nl_geterror(err)));
        }

        err = nl_socket_add_memberships(socket,
                                        RTNLGRP_LINK,
                                        RTNLGRP_IPV4_IFADDR,
                                        RTNLGRP_IPV6_IFADDR,
                                        0);
        if (err < 0) {
            close_socket();
            throw InterfaceMonitorError(
                format("Failed to subscribe interface monitor to link group: {}", nl_geterror(err)));
        }

        nl_socket_set_nonblocking(socket);
        nl_socket_disable_seq_check(socket);
        nl_socket_modify_cb(socket,
                            NL_CB_VALID,
                            NL_CB_CUSTOM,
                            &InterfaceMonitor::Impl::on_nl_message,
                            this);

        interface_state.clear();
        struct ifaddrs* interfaces = nullptr;
        if (getifaddrs(&interfaces) == 0) {
            for (auto* current = interfaces; current != nullptr; current = current->ifa_next) {
                if (current->ifa_name != nullptr) {
                    interface_state[current->ifa_name] = (current->ifa_flags & IFF_UP) != 0;
                }
            }
            freeifaddrs(interfaces);
        }
    }

    InterfaceStateCallback callback;
    struct nl_sock* socket{nullptr};
    std::unordered_map<std::string, bool> interface_state;
};

InterfaceMonitor::InterfaceMonitor(InterfaceStateCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {
    impl_->setup_socket();
}

InterfaceMonitor::~InterfaceMonitor() = default;

int InterfaceMonitor::fd() const {
    if (!impl_ || !impl_->socket) {
        throw InterfaceMonitorError("Interface monitor socket is not initialized");
    }
    return nl_socket_get_fd(impl_->socket);
}

void InterfaceMonitor::handle_events() {
    if (!impl_ || !impl_->socket) {
        return;
    }

    while (true) {
        int err = nl_recvmsgs_default(impl_->socket);
        if (err == 0) {
            continue;
        }

        if (err == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        throw InterfaceMonitorError(
            format("Failed to receive interface monitor netlink messages: {}", nl_geterror(err)));
    }
}

void InterfaceMonitor::reconnect() {
    if (!impl_) {
        return;
    }

    impl_->setup_socket();
}

} // namespace keen_pbr3
