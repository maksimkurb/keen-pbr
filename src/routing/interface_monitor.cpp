#include "interface_monitor.hpp"

#include "../util/format_compat.hpp"

#include <cerrno>
#include <cstring>
#include <linux/if.h>
#include <linux/rtnetlink.h>
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
        if (socket) {
            nl_close(socket);
            nl_socket_free(socket);
            socket = nullptr;
        }
    }

    static int on_nl_message(struct nl_msg* msg, void* arg) {
        auto* impl = static_cast<Impl*>(arg);
        if (!impl) {
            return NL_OK;
        }

        impl->handle_link_message(msg);
        return NL_OK;
    }

    void handle_link_message(struct nl_msg* msg) {
        if (!msg || !callback) {
            return;
        }

        struct nlmsghdr* hdr = nlmsg_hdr(msg);
        if (!hdr) {
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
        if (previous != interface_state.end() && previous->second == is_up) {
            return;
        }

        interface_state[interface_name] = is_up;
        callback(interface_name, is_up);
    }

    InterfaceStateCallback callback;
    struct nl_sock* socket{nullptr};
    std::unordered_map<std::string, bool> interface_state;
};

InterfaceMonitor::InterfaceMonitor(InterfaceStateCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {
    impl_->socket = nl_socket_alloc();
    if (!impl_->socket) {
        throw InterfaceMonitorError("Failed to allocate netlink socket for interface monitor");
    }

    int err = nl_connect(impl_->socket, NETLINK_ROUTE);
    if (err < 0) {
        throw InterfaceMonitorError(
            format("Failed to connect interface monitor netlink socket: {}", nl_geterror(err)));
    }

    err = nl_socket_add_memberships(impl_->socket, RTNLGRP_LINK, 0);
    if (err < 0) {
        throw InterfaceMonitorError(
            format("Failed to subscribe interface monitor to link group: {}", nl_geterror(err)));
    }

    nl_socket_set_nonblocking(impl_->socket);
    nl_socket_disable_seq_check(impl_->socket);
    nl_socket_modify_cb(impl_->socket,
                        NL_CB_VALID,
                        NL_CB_CUSTOM,
                        &InterfaceMonitor::Impl::on_nl_message,
                        impl_.get());
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

} // namespace keen_pbr3
