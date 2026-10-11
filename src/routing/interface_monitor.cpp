#include "interface_monitor.hpp"

#include "../util/format_compat.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <ifaddrs.h>
#include <linux/filter.h>
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
// Keenetic reinstalls every routing table at once on each WAN DHCP renewal:
// a few hundred route messages in under a second.  The kernel charges only
// what is queued, so the cap costs nothing while the queue is drained.
constexpr int kReceiveBufferBytes = 1 << 20;

void set_receive_buffer(struct nl_sock* socket) {
    const int fd = nl_socket_get_fd(socket);
    int size = kReceiveBufferBytes;
    // FORCE ignores net.core.rmem_max; it needs CAP_NET_ADMIN, which the
    // daemon has.  The fallback is capped by rmem_max.
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) != 0) {
        nl_socket_set_buffer_size(socket, kReceiveBufferBytes, 0);
    }
}

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

        if (hdr->nlmsg_type == RTM_NEWROUTE || hdr->nlmsg_type == RTM_DELROUTE) {
            auto* route = static_cast<rtmsg*>(nlmsg_data(hdr));
            if (route != nullptr &&
                (route->rtm_family == AF_INET || route->rtm_family == AF_INET6) &&
                route->rtm_table == RT_TABLE_MAIN) {
                Event event{"", false, false, true};
                if (route->rtm_dst_len == 0) {
                    event.default_route_changed = true;
                }
                callback(event);
            }
            return;
        }

        if (hdr->nlmsg_type == RTM_NEWADDR || hdr->nlmsg_type == RTM_DELADDR) {
            auto* addr = static_cast<ifaddrmsg*>(nlmsg_data(hdr));
            if (!addr || (addr->ifa_family != AF_INET && addr->ifa_family != AF_INET6)) {
                return;
            }
            char name[IF_NAMESIZE] = {};
            if (if_indextoname(addr->ifa_index, name) != nullptr) {
                callback(Event{std::string(name), false, false, false, true});
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
        const unsigned int flags = if_info->ifi_flags;
        const bool is_up = (hdr->nlmsg_type == RTM_NEWLINK) && ((flags & IFF_UP) != 0);

        const auto previous = interface_state.find(interface_name);
        // Wireless drivers repeat RTM_NEWLINK for stats and wireless events,
        // several times a second, with the flags unchanged.  Carrier changes
        // still pass: they flip IFF_LOWER_UP/IFF_RUNNING.
        if (hdr->nlmsg_type == RTM_NEWLINK && previous != interface_state.end() &&
            previous->second == flags) {
            return;
        }
        const bool administrative_state_changed =
            hdr->nlmsg_type == RTM_NEWLINK &&
            previous != interface_state.end() &&
            ((previous->second & IFF_UP) != 0) != is_up;
        if (hdr->nlmsg_type == RTM_DELLINK) {
            interface_state.erase(interface_name);
        } else {
            interface_state[interface_name] = flags;
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
        set_receive_buffer(socket);
        // Best effort: without the filter the other tables' routes are still
        // ignored, they only take queue space.
        attach_main_table_route_filter(nl_socket_get_fd(socket));

        err = nl_socket_add_memberships(socket,
                                        RTNLGRP_LINK,
                                        RTNLGRP_IPV4_IFADDR,
                                        RTNLGRP_IPV6_IFADDR,
                                        RTNLGRP_IPV4_ROUTE,
                                        RTNLGRP_IPV6_ROUTE,
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

        load_interface_state();
    }

    void load_interface_state() {
        interface_state.clear();
        struct ifaddrs* interfaces = nullptr;
        if (getifaddrs(&interfaces) == 0) {
            for (auto* current = interfaces; current != nullptr; current = current->ifa_next) {
                if (current->ifa_name != nullptr) {
                    interface_state[current->ifa_name] = current->ifa_flags;
                }
            }
            freeifaddrs(interfaces);
        }
    }

    InterfaceStateCallback callback;
    struct nl_sock* socket{nullptr};
    // Last seen ifi_flags per interface.
    std::unordered_map<std::string, unsigned int> interface_state;
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

    bool overflowed = false;
    while (true) {
        int err = nl_recvmsgs_default(impl_->socket);
        if (err == 0) {
            continue;
        }

        // libnl reports ENOBUFS, a receive queue overflow, as NLE_NOMEM. The
        // socket stays usable; only the dropped notifications are lost.
        if (err == -NLE_NOMEM) {
            overflowed = true;
            continue;
        }

        if (err == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        throw InterfaceMonitorError(
            format("Failed to receive interface monitor netlink messages: {}", nl_geterror(err)));
    }

    if (overflowed) {
        impl_->load_interface_state();
        if (impl_->callback) {
            Event event;
            event.resync = true;
            impl_->callback(event);
        }
    }
}

bool InterfaceMonitor::attach_main_table_route_filter(int fd) noexcept {
    // Route notifications carry one message per skb.  nlmsg_type is in host
    // order and BPF loads are big-endian, hence htons().  rtm_table holds
    // RT_TABLE_MAIN as is; tables above 255 show as RT_TABLE_COMPAT.
    std::array<sock_filter, 7> code{{
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, offsetof(nlmsghdr, nlmsg_type)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_NEWROUTE), 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_DELROUTE), 0, 2),
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, NLMSG_HDRLEN + offsetof(rtmsg, rtm_table)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, RT_TABLE_MAIN, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFFU),
        BPF_STMT(BPF_RET | BPF_K, 0),
    }};
    sock_fprog program{static_cast<unsigned short>(code.size()), code.data()};
    return setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program)) == 0;
}

void InterfaceMonitor::reconnect() {
    if (!impl_) {
        return;
    }

    impl_->setup_socket();
}

} // namespace keen_pbr3
