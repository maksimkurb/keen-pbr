#include <doctest/doctest.h>

#include "../src/netfilter/conntrack.hpp"
#include "../src/netfilter/nflog.hpp"
#include "../src/netfilter/nfqueue.hpp"

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace keen_pbr3;
using namespace keen_pbr3::nfnl;

namespace {

MsgView one_message(const MsgBuilder& builder) {
    MsgView result;
    REQUIRE(for_each_msg(ByteView(builder.data(), builder.size()), [&](const MsgView& message) {
        result = message;
        return false;
    }));
    return result;
}

ConntrackEntry ipv4_entry() {
    ConntrackEntry entry;
    entry.original.family = ConntrackFamily::ipv4;
    entry.original.protocol = IPPROTO_UDP;
    entry.original.src = {192, 0, 2, 10};
    entry.original.dst = {198, 51, 100, 20};
    entry.original.src_port = 53000;
    entry.original.dst_port = 443;
    entry.original.zone = 7;
    entry.original.has_zone = true;
    entry.original.zone_scope = ConntrackZoneScope::top;
    entry.mark = 0x12345678;
    entry.id = 0x10203040;
    entry.has_id = true;
    return entry;
}

void put_orig(MsgBuilder& builder, const ConntrackEntry& entry,
              bool nested_zone = false, uint16_t tuple_type = CTA_TUPLE_ORIG) {
    const auto& tuple = entry.original;
    const std::size_t orig = builder.nest_begin(tuple_type);
    const auto& source = tuple_type == CTA_TUPLE_REPLY ? tuple.dst : tuple.src;
    const auto& destination = tuple_type == CTA_TUPLE_REPLY ? tuple.src : tuple.dst;
    const uint16_t source_port = tuple_type == CTA_TUPLE_REPLY ? tuple.dst_port : tuple.src_port;
    const uint16_t destination_port =
        tuple_type == CTA_TUPLE_REPLY ? tuple.src_port : tuple.dst_port;
    const std::size_t ip = builder.nest_begin(CTA_TUPLE_IP);
    if (tuple.family == ConntrackFamily::ipv4) {
        builder.put(CTA_IP_V4_SRC, source.data(), 4);
        builder.put(CTA_IP_V4_DST, destination.data(), 4);
    } else {
        builder.put(CTA_IP_V6_SRC, source.data(), 16);
        builder.put(CTA_IP_V6_DST, destination.data(), 16);
    }
    builder.nest_end(ip);
    const std::size_t proto = builder.nest_begin(CTA_TUPLE_PROTO);
    builder.put_u8(CTA_PROTO_NUM, tuple.protocol);
    builder.put_u16_be(CTA_PROTO_SRC_PORT, source_port);
    builder.put_u16_be(CTA_PROTO_DST_PORT, destination_port);
    builder.nest_end(proto);
    if (nested_zone && tuple.has_zone) builder.put_u16_be(CTA_TUPLE_ZONE, tuple.zone);
    builder.nest_end(orig);
}

bool isolated_netns_guard() {
    const char* enabled = std::getenv("KPBR_NETLINK_IT");
    const char* guard = std::getenv("KPBR_NETLINK_IN_NETNS");
    const char* parent_id = std::getenv("KPBR_NETLINK_PARENT_NS");
    if (enabled == nullptr || std::string(enabled) != "1" || guard == nullptr ||
        std::string(guard) != "1" || parent_id == nullptr || std::string(parent_id).empty()) {
        return false;
    }
    struct stat self{};
    if (::stat("/proc/self/ns/net", &self) != 0) return false;
    const std::string current = std::to_string(self.st_dev) + ":" +
                                std::to_string(self.st_ino);
    return current != parent_id;
}

bool live_wrappers_enabled() {
    const char* enabled = std::getenv("KPBR_NFNETLINK_LIVE");
    return enabled != nullptr && std::string(enabled) == "1" && isolated_netns_guard();
}

bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

int shell(const char* command) { return std::system(command); }

bool receive_udp(int fd, std::string& payload) {
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, 1000) <= 0) return false;
    char buffer[2048];
    const ssize_t size = ::recv(fd, buffer, sizeof(buffer), 0);
    if (size < 0) return false;
    payload.assign(buffer, buffer + size);
    return true;
}

uint16_t bound_port(int fd, int family) {
    if (family == AF_INET) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        return ntohs(addr.sin_port);
    }
    sockaddr_in6 addr{};
    socklen_t len = sizeof(addr);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    return ntohs(addr.sin6_port);
}

uint32_t checksum_sum(const uint8_t* data, std::size_t length, uint32_t sum = 0) {
    while (length >= 2) {
        sum += (static_cast<uint32_t>(data[0]) << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length != 0) sum += static_cast<uint32_t>(data[0]) << 8;
    while ((sum >> 16) != 0) sum = (sum & 0xffffu) + (sum >> 16);
    return sum;
}

uint16_t checksum16(const uint8_t* data, std::size_t length, uint32_t sum = 0) {
    sum = checksum_sum(data, length, sum);
    return static_cast<uint16_t>(~sum);
}

uint16_t read_u16_be(const uint8_t* data) {
    uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return ntohs(value);
}

void write_u16_be(uint8_t* data, uint16_t value) {
    const uint16_t network = htons(value);
    std::memcpy(data, &network, sizeof(network));
}

bool rewrite_udp_payload(std::vector<uint8_t>& packet, std::string_view replacement) {
    if (packet.size() < 20 || (packet[0] >> 4) != 4 || (packet[9] != IPPROTO_UDP)) return false;
    const std::size_t ihl = static_cast<std::size_t>(packet[0] & 0x0f) * 4;
    if (ihl < 20 || packet.size() < ihl + 8) return false;
    const uint16_t ip_length = read_u16_be(packet.data() + 2);
    const std::size_t udp_length = read_u16_be(packet.data() + ihl + 4);
    if (udp_length < 8 || ihl + udp_length > packet.size() || ip_length > packet.size()) return false;
    const std::size_t payload_size = udp_length - 8;
    if (replacement.size() != payload_size) return false;
    std::memcpy(packet.data() + ihl + 8, replacement.data(), replacement.size());
    write_u16_be(packet.data() + ihl + 6, 0);
    uint32_t sum = 0;
    sum = checksum_sum(packet.data() + 12, 8, sum);
    sum += IPPROTO_UDP;
    sum += static_cast<uint16_t>(udp_length);
    const uint16_t checksum = checksum16(packet.data() + ihl, udp_length, sum);
    write_u16_be(packet.data() + ihl + 6, checksum == 0 ? 0xffff : checksum);
    // OUTPUT packets may still carry a zero/offloaded IPv4 header checksum;
    // replacement reinjection disables skb checksum offload, so provide the
    // final L3 checksum as well as the UDP pseudo-header checksum.
    write_u16_be(packet.data() + 10, 0);
    write_u16_be(packet.data() + 10, checksum16(packet.data(), ihl));
    return true;
}

bool valid_udp_checksum(const std::vector<uint8_t>& packet) {
    if (packet.size() < 20 || (packet[0] >> 4) != 4 || packet[9] != IPPROTO_UDP) return false;
    const std::size_t ihl = static_cast<std::size_t>(packet[0] & 0x0f) * 4;
    if (ihl < 20 || packet.size() < ihl + 8) return false;
    const std::size_t udp_length = read_u16_be(packet.data() + ihl + 4);
    if (udp_length < 8 || ihl + udp_length > packet.size()) return false;
    uint32_t sum = checksum_sum(packet.data() + 12, 8);
    sum += IPPROTO_UDP;
    sum += static_cast<uint16_t>(udp_length);
    return checksum16(packet.data() + ihl, udp_length, sum) == 0;
}

bool valid_ip_checksum(const std::vector<uint8_t>& packet) {
    if (packet.size() < 20 || (packet[0] >> 4) != 4) return false;
    const std::size_t ihl = static_cast<std::size_t>(packet[0] & 0x0f) * 4;
    return ihl >= 20 && ihl <= packet.size() && checksum16(packet.data(), ihl) == 0;
}

struct UdpFlow {
    int server{-1};
    int client{-1};
    uint16_t source_port{0};
    uint16_t destination_port{0};
};

struct TcpFlow {
    int server{-1};
    int client{-1};
    int accepted{-1};
    uint16_t source_port{0};
    uint16_t destination_port{0};
};

UdpFlow make_udp_flow(int family, uint16_t destination_port) {
    UdpFlow flow;
    flow.server = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    flow.client = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(flow.server >= 0);
    REQUIRE(flow.client >= 0);
    if (family == AF_INET) {
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(destination_port);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(flow.server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0);
        sockaddr_in client_addr{};
        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(flow.client, reinterpret_cast<sockaddr*>(&client_addr), sizeof(client_addr)) == 0);
        REQUIRE(::sendto(flow.client, "nfct-flow", 9, 0,
                         reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 9);
    } else {
        sockaddr_in6 server_addr{};
        server_addr.sin6_family = AF_INET6;
        server_addr.sin6_port = htons(destination_port);
        server_addr.sin6_addr = in6addr_loopback;
        REQUIRE(::bind(flow.server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0);
        sockaddr_in6 client_addr{};
        client_addr.sin6_family = AF_INET6;
        client_addr.sin6_addr = in6addr_loopback;
        REQUIRE(::bind(flow.client, reinterpret_cast<sockaddr*>(&client_addr), sizeof(client_addr)) == 0);
        REQUIRE(::sendto(flow.client, "nfct-flow", 9, 0,
                         reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 9);
    }
    flow.source_port = bound_port(flow.client, family);
    flow.destination_port = destination_port;
    return flow;
}

void close_flow(UdpFlow& flow) {
    if (flow.server >= 0) ::close(flow.server);
    if (flow.client >= 0) ::close(flow.client);
    flow.server = flow.client = -1;
}

TcpFlow make_tcp_flow(int family, uint16_t destination_port) {
    TcpFlow flow;
    flow.server = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    flow.client = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(flow.server >= 0);
    REQUIRE(flow.client >= 0);
    const int one = 1;
    REQUIRE(::setsockopt(flow.server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0);
    sockaddr_storage address{};
    socklen_t address_len = 0;
    if (family == AF_INET) {
        auto* v4 = reinterpret_cast<sockaddr_in*>(&address);
        v4->sin_family = AF_INET;
        v4->sin_port = htons(destination_port);
        v4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address_len = sizeof(*v4);
    } else {
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&address);
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(destination_port);
        v6->sin6_addr = in6addr_loopback;
        address_len = sizeof(*v6);
    }
    REQUIRE(::bind(flow.server, reinterpret_cast<sockaddr*>(&address), address_len) == 0);
    REQUIRE(::listen(flow.server, 1) == 0);
    REQUIRE(::connect(flow.client, reinterpret_cast<sockaddr*>(&address), address_len) == 0);
    flow.accepted = ::accept4(flow.server, nullptr, nullptr, SOCK_CLOEXEC);
    REQUIRE(flow.accepted >= 0);
    flow.source_port = bound_port(flow.client, family);
    flow.destination_port = destination_port;
    REQUIRE(::send(flow.client, "tcp-flow", 8, 0) == 8);
    return flow;
}

TcpFlow make_tcp_flow(uint16_t destination_port) {
    return make_tcp_flow(AF_INET, destination_port);
}

void close_flow(TcpFlow& flow) {
    if (flow.accepted >= 0) ::close(flow.accepted);
    if (flow.server >= 0) ::close(flow.server);
    if (flow.client >= 0) ::close(flow.client);
    flow.accepted = flow.server = flow.client = -1;
}

std::vector<ConntrackEntry> dump_entries(ConntrackFamily family) {
    ConntrackDump dump(family);
    for (int i = 0; i < 50 && !dump.complete(); ++i) {
        const int rc = dump.receive(100);
        REQUIRE(rc >= 0);
    }
    REQUIRE(dump.complete());
    return dump.entries();
}

const ConntrackEntry* find_udp(const std::vector<ConntrackEntry>& entries,
                               ConntrackFamily family, uint16_t source_port,
                               uint16_t destination_port) {
    for (const auto& entry : entries) {
        if (entry.original.family == family && entry.original.protocol == IPPROTO_UDP &&
            entry.original.src_port == source_port && entry.original.dst_port == destination_port) {
            return &entry;
        }
    }
    return nullptr;
}

const ConntrackEntry* find_protocol(const std::vector<ConntrackEntry>& entries,
                                    ConntrackFamily family, uint8_t protocol,
                                    uint16_t source_port, uint16_t destination_port) {
    for (const auto& entry : entries) {
        if (entry.original.family == family && entry.original.protocol == protocol &&
            entry.original.src_port == source_port && entry.original.dst_port == destination_port) {
            return &entry;
        }
    }
    return nullptr;
}

bool contains_bytes(ByteView view, std::string_view needle) {
    if (needle.empty() || view.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= view.size(); ++i) {
        if (std::memcmp(view.data() + i, needle.data(), needle.size()) == 0) return true;
    }
    return false;
}

std::vector<uint8_t> control_datagram(uint16_t type, uint16_t flags, uint32_t seq,
                                      const int32_t* payload = nullptr) {
    const std::size_t payload_size = payload == nullptr ? 0 : sizeof(*payload);
    std::vector<uint8_t> bytes(sizeof(nlmsghdr) + payload_size, 0);
    nlmsghdr header{};
    header.nlmsg_len = static_cast<uint32_t>(bytes.size());
    header.nlmsg_type = type;
    header.nlmsg_flags = flags;
    header.nlmsg_seq = seq;
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (payload != nullptr) std::memcpy(bytes.data() + sizeof(header), payload, payload_size);
    return bytes;
}

int drain_udp(int fd, int timeout_ms) {
    int count = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    char payload[2048];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) break;
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(left));
        if (ready <= 0) break;
        if (::recv(fd, payload, sizeof(payload), 0) >= 0) ++count;
    }
    return count;
}

} // namespace

TEST_CASE("nflog: config uses scoped group and copy range") {
    NfLogOptions options;
    options.group = 9054;
    options.copy_range = 2048;
    MsgBuilder builder;
    build_nflog_config(builder, 9, options);
    const MsgView message = one_message(builder);
    CHECK(message.type == ((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG));
    CHECK(message.res_id == 9054);
    Attr attrs[NFULA_CFG_MAX + 1]{};
    REQUIRE(parse_attrs(message.attrs, attrs, NFULA_CFG_MAX + 1));
    REQUIRE(attrs[NFULA_CFG_MODE].payload.size() == sizeof(nfulnl_msg_config_mode));
    nfulnl_msg_config_mode mode{};
    std::memcpy(&mode, attrs[NFULA_CFG_MODE].payload.data(), sizeof(mode));
    CHECK(ntohl(mode.copy_range) == 2048);
    CHECK(mode.copy_mode == NFULNL_COPY_PACKET);
    CHECK(attr_u32_be(attrs[NFULA_CFG_QTHRESH]) == 1);
    CHECK(attr_u32_be(attrs[NFULA_CFG_TIMEOUT]) == 1);
}

TEST_CASE("nflog: packet parser accepts metadata and payload") {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET),
                  0, 10, AF_INET, 9054);
    nfulnl_msg_packet_hdr header{};
    header.hw_protocol = htons(ETH_P_IP);
    header.hook = 3;
    builder.put(NFULA_PACKET_HDR, &header, sizeof(header));
    builder.put_u32_be(NFULA_MARK, 0xfeedbeef);
    builder.put_u32_be(NFULA_IFINDEX_INDEV, 2);
    const uint8_t payload[] = {0x45, 0x00, 0x00, 0x14};
    builder.put(NFULA_PAYLOAD, payload, sizeof(payload));
    builder.end();
    LoggedPacket packet;
    REQUIRE(parse_nflog_packet(one_message(builder), packet));
    CHECK(packet.hw_protocol == ETH_P_IP);
    CHECK(packet.hook == 3);
    CHECK(packet.mark == 0xfeedbeef);
    CHECK(packet.indev == 2);
    CHECK(packet.payload.size() == sizeof(payload));
}

TEST_CASE("nflog: malformed packet is rejected") {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET),
                  0, 10, AF_INET, 9054);
    const uint8_t short_header[] = {1, 2};
    builder.put(NFULA_PACKET_HDR, short_header, sizeof(short_header));
    builder.end();
    LoggedPacket packet;
    CHECK_FALSE(parse_nflog_packet(one_message(builder), packet));
}

TEST_CASE("conntrack: parser preserves original tuple, zone, mark and id") {
    const ConntrackEntry expected = ipv4_entry();
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
                  0, 20, AF_INET, 0);
    put_orig(builder, expected);
    builder.put_u32_be(CTA_MARK, expected.mark);
    builder.put_u16_be(CTA_ZONE, expected.original.zone);
    builder.put_u32_be(CTA_ID, expected.id);
    builder.end();
    ConntrackEntry actual;
    REQUIRE(parse_conntrack_entry(one_message(builder), actual));
    CHECK(actual.original == expected.original);
    CHECK(actual.mark == expected.mark);
    CHECK(actual.has_id);
    CHECK(actual.id == expected.id);
}

TEST_CASE("conntrack: directional zone provenance controls delete encoding") {
    ConntrackEntry expected = ipv4_entry();
    expected.original.zone_scope = ConntrackZoneScope::original;

    MsgBuilder original_message;
    original_message.begin(
        static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
        0, 26, AF_INET, 0);
    put_orig(original_message, expected, true);
    original_message.end();
    ConntrackEntry original;
    REQUIRE(parse_conntrack_entry(one_message(original_message), original));
    CHECK(original.original.zone == expected.original.zone);
    CHECK(original.original.zone_scope == ConntrackZoneScope::original);
    CHECK(original.deletion_candidate());

    MsgBuilder delete_message;
    build_conntrack_delete(delete_message, 27, original);
    Attr delete_attrs[CTA_MAX + 1]{};
    REQUIRE(parse_attrs(one_message(delete_message).attrs, delete_attrs, CTA_MAX + 1));
    CHECK(delete_attrs[CTA_ZONE].payload.size() == 0);
    Attr delete_tuple[CTA_TUPLE_MAX + 1]{};
    REQUIRE(parse_attrs(delete_attrs[CTA_TUPLE_ORIG].payload, delete_tuple,
                        CTA_TUPLE_MAX + 1));
    CHECK(attr_u16_be(delete_tuple[CTA_TUPLE_ZONE]) == expected.original.zone);

    MsgBuilder reply_message;
    reply_message.begin(
        static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
        0, 28, AF_INET, 0);
    put_orig(reply_message, expected);
    put_orig(reply_message, expected, true, CTA_TUPLE_REPLY);
    reply_message.end();
    ConntrackEntry reply_only;
    REQUIRE(parse_conntrack_entry(one_message(reply_message), reply_only));
    CHECK(reply_only.original.zone == expected.original.zone);
    CHECK(reply_only.original.zone_scope == ConntrackZoneScope::reply);
    REQUIRE(reply_only.reply.has_value());
    CHECK(reply_only.reply->src == expected.original.dst);
    CHECK(reply_only.reply->dst == expected.original.src);
    CHECK(reply_only.reply->src_port == expected.original.dst_port);
    CHECK(reply_only.reply->dst_port == expected.original.src_port);
    CHECK(reply_only.deletion_candidate());

    MsgBuilder reply_delete;
    build_conntrack_delete(reply_delete, 29, reply_only);
    Attr reply_delete_attrs[CTA_MAX + 1]{};
    REQUIRE(parse_attrs(one_message(reply_delete).attrs, reply_delete_attrs, CTA_MAX + 1));
    CHECK(reply_delete_attrs[CTA_ZONE].payload.size() == 0);
    CHECK(reply_delete_attrs[CTA_TUPLE_ORIG].payload.size() == 0);
    Attr reply_delete_tuple[CTA_TUPLE_MAX + 1]{};
    REQUIRE(parse_attrs(reply_delete_attrs[CTA_TUPLE_REPLY].payload, reply_delete_tuple,
                        CTA_TUPLE_MAX + 1));
    CHECK(attr_u16_be(reply_delete_tuple[CTA_TUPLE_ZONE]) == expected.original.zone);

    MsgBuilder incomplete_reply_message;
    incomplete_reply_message.begin(
        static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
        0, 30, AF_INET, 0);
    put_orig(incomplete_reply_message, expected);
    const std::size_t incomplete_reply =
        incomplete_reply_message.nest_begin(CTA_TUPLE_REPLY);
    incomplete_reply_message.put_u16_be(CTA_TUPLE_ZONE, expected.original.zone);
    incomplete_reply_message.nest_end(incomplete_reply);
    incomplete_reply_message.end();
    ConntrackEntry skipped_entry;
    REQUIRE(parse_conntrack_entry(one_message(incomplete_reply_message), skipped_entry));
    CHECK_FALSE(skipped_entry.deletion_candidate());
    ConntrackDeleteBatch skipped({skipped_entry});
    CHECK(skipped.complete());
    CHECK(skipped.skipped() == 1);
}

TEST_CASE("conntrack: requests use AF_INET and AF_INET6 netlink families") {
    MsgBuilder v4;
    build_conntrack_get(v4, 24, ConntrackFamily::ipv4);
    CHECK(one_message(v4).family == AF_INET);
    MsgBuilder v6;
    build_conntrack_get(v6, 25, ConntrackFamily::ipv6);
    CHECK(one_message(v6).family == AF_INET6);
}

TEST_CASE("conntrack: delete contains full tuple and identity") {
    const ConntrackEntry expected = ipv4_entry();
    MsgBuilder builder;
    build_conntrack_delete(builder, 21, expected);
    const MsgView message = one_message(builder);
    CHECK(message.type == ((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_DELETE));
    CHECK((message.flags & NLM_F_ACK) != 0);
    Attr attrs[CTA_MAX + 1]{};
    REQUIRE(parse_attrs(message.attrs, attrs, CTA_MAX + 1));
    CHECK(attr_u16_be(attrs[CTA_ZONE]) == expected.original.zone);
    CHECK(attr_u32_be(attrs[CTA_ID]) == expected.id);
    Attr tuple_attrs[CTA_TUPLE_MAX + 1]{};
    REQUIRE(parse_attrs(attrs[CTA_TUPLE_ORIG].payload, tuple_attrs, CTA_TUPLE_MAX + 1));
    Attr ip_attrs[CTA_IP_MAX + 1]{};
    REQUIRE(parse_attrs(tuple_attrs[CTA_TUPLE_IP].payload, ip_attrs, CTA_IP_MAX + 1));
    CHECK(std::memcmp(ip_attrs[CTA_IP_V4_SRC].payload.data(), expected.original.src.data(), 4) == 0);
    CHECK(std::memcmp(ip_attrs[CTA_IP_V4_DST].payload.data(), expected.original.dst.data(), 4) == 0);
    Attr proto_attrs[CTA_PROTO_MAX + 1]{};
    REQUIRE(parse_attrs(tuple_attrs[CTA_TUPLE_PROTO].payload, proto_attrs, CTA_PROTO_MAX + 1));
    CHECK(attr_u8(proto_attrs[CTA_PROTO_NUM]) == expected.original.protocol);
    CHECK(attr_u16_be(proto_attrs[CTA_PROTO_SRC_PORT]) == expected.original.src_port);
    CHECK(attr_u16_be(proto_attrs[CTA_PROTO_DST_PORT]) == expected.original.dst_port);
    CHECK(tuple_attrs[CTA_TUPLE_ZONE].payload.size() == 0);
}

TEST_CASE("conntrack: malformed or incomplete tuple is never a delete candidate") {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
                  0, 22, AF_INET, 0);
    const std::size_t orig = builder.nest_begin(CTA_TUPLE_ORIG);
    const std::size_t ip = builder.nest_begin(CTA_TUPLE_IP);
    const uint8_t only_source[] = {192, 0, 2, 1};
    builder.put(CTA_IP_V4_SRC, only_source, sizeof(only_source));
    builder.nest_end(ip);
    builder.nest_end(orig);
    builder.end();
    ConntrackEntry entry;
    CHECK_FALSE(parse_conntrack_entry(one_message(builder), entry));
}

TEST_CASE("conntrack: ICMP protocol key is retained without fake ports") {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW),
                  0, 23, AF_INET, 0);
    ConntrackEntry expected;
    expected.original.family = ConntrackFamily::ipv4;
    expected.original.protocol = IPPROTO_ICMP;
    expected.original.src = {192, 0, 2, 2};
    expected.original.dst = {192, 0, 2, 3};
    expected.original.has_ports = false;
    expected.original.has_icmp_key = true;
    expected.original.icmp_id = 44;
    expected.original.icmp_type = 8;
    expected.original.icmp_code = 0;
    const std::size_t orig = builder.nest_begin(CTA_TUPLE_ORIG);
    const std::size_t ip = builder.nest_begin(CTA_TUPLE_IP);
    builder.put(CTA_IP_V4_SRC, expected.original.src.data(), 4);
    builder.put(CTA_IP_V4_DST, expected.original.dst.data(), 4);
    builder.nest_end(ip);
    const std::size_t proto = builder.nest_begin(CTA_TUPLE_PROTO);
    builder.put_u8(CTA_PROTO_NUM, expected.original.protocol);
    builder.put_u16_be(CTA_PROTO_ICMP_ID, expected.original.icmp_id);
    builder.put_u8(CTA_PROTO_ICMP_TYPE, expected.original.icmp_type);
    builder.put_u8(CTA_PROTO_ICMP_CODE, expected.original.icmp_code);
    builder.nest_end(proto);
    builder.nest_end(orig);
    builder.end();
    ConntrackEntry actual;
    REQUIRE(parse_conntrack_entry(one_message(builder), actual));
    CHECK(actual.original == expected.original);
}

TEST_CASE("conntrack: dump completion surfaces DONE error and interruption") {
    const int error = -EIO;
    int parsed = 0;
    MsgView message;
    message.type = NLMSG_DONE;
    message.attrs = ByteView(reinterpret_cast<const uint8_t*>(&error), sizeof(error));
    REQUIRE(parse_done_error(message, parsed));
    CHECK(parsed == EIO);

    message.flags = NLM_F_DUMP_INTR;
    message.attrs = ByteView();
    REQUIRE(parse_done_error(message, parsed));
    CHECK(parsed == EINTR);
}

TEST_CASE("conntrack: dump state handles ACK, malformed DONE and interruption") {
    if (!live_wrappers_enabled() || !env_enabled("KPBR_CONNTRACK_STATE_LIVE")) return;
    try {
        const int32_t ack = 0;
        ConntrackDump acknowledged(ConntrackFamily::ipv4);
        const auto ack_datagram = control_datagram(NLMSG_ERROR, 0,
                                                   acknowledged.sequence_for_test(), &ack);
        CHECK(acknowledged.consume_datagram_for_test(
                  ByteView(ack_datagram.data(), ack_datagram.size())) == 0);
        CHECK_FALSE(acknowledged.complete());
        CHECK(acknowledged.last_errno() == 0);

        const int32_t failure = -EIO;
        ConntrackDump failed(ConntrackFamily::ipv4);
        const auto error_datagram = control_datagram(NLMSG_ERROR, 0,
                                                     failed.sequence_for_test(), &failure);
        CHECK(failed.consume_datagram_for_test(
                  ByteView(error_datagram.data(), error_datagram.size())) == -1);
        CHECK(failed.last_errno() == EIO);

        ConntrackDump interrupted(ConntrackFamily::ipv4);
        const auto done_datagram = control_datagram(NLMSG_DONE, NLM_F_DUMP_INTR,
                                                    interrupted.sequence_for_test());
        CHECK(interrupted.consume_datagram_for_test(
                  ByteView(done_datagram.data(), done_datagram.size())) == -1);
        CHECK(interrupted.last_errno() == EINTR);

        ConntrackDump malformed(ConntrackFamily::ipv4);
        const std::array<uint8_t, 3> short_datagram{0, 0, 0};
        CHECK(malformed.consume_datagram_for_test(
                  ByteView(short_datagram.data(), short_datagram.size())) == -1);
        CHECK(malformed.last_errno() == EPROTO);
    } catch (const std::exception& error) {
        INFO(error.what());
        CHECK(false);
    }
}

TEST_CASE("nfqueue: live ACCEPT and payload replacement (isolated netns only)") {
    if (!live_wrappers_enabled()) return;
    // Set KPBR_NFQUEUE_REPLACEMENT_LIVE=1 to require the replacement path;
    // the default live smoke isolates capture + ACCEPT while replacement
    // reinjection remains an explicit diagnostic test.
    const bool replace_live = env_enabled("KPBR_NFQUEUE_REPLACEMENT_LIVE");
    constexpr uint16_t port = 39053;
    constexpr uint16_t queue_number = 9053;
    constexpr const char* rule =
        "iptables -t mangle -I POSTROUTING 1 -p udp --dport 39053 -j NFQUEUE --queue-num 9053 --queue-bypass";
    constexpr const char* cleanup =
        "iptables -t mangle -D POSTROUTING -p udp --dport 39053 -j NFQUEUE --queue-num 9053 --queue-bypass";
    const int server = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    const int client = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(client >= 0);
    sockaddr_in server_addr{};
    REQUIRE(server >= 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0);
    REQUIRE(shell(rule) == 0);
    bool got_packet = false;
    bool replacement_sent = false;
    std::size_t captured_size = 0;
    bool replacement_checksum_valid = false;
    bool replacement_ip_checksum_valid = false;
    uint16_t original_checksum = 0;
    uint16_t replacement_checksum = 0;
    try {
        NfQueueOptions options;
        options.queue_num = queue_number;
        options.copy_range = 0xffff;
        NfQueue queue(options);
        if (replace_live && !queue.payload_replacement_supported()) {
            INFO("SKIP NFQUEUE replacement: kernel requires the initial user namespace for NFQA_PAYLOAD");
            CHECK_FALSE(queue.payload_replacement_supported());
            CHECK(shell(cleanup) == 0);
            ::close(client);
            ::close(server);
            return;
        }
        CHECK(queue.receive([](const QueuedPacket&) {}) == 0);
        const char original[] = "nfq-old";
        REQUIRE(::sendto(client, original, sizeof(original) - 1, 0,
                         reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) ==
                static_cast<ssize_t>(sizeof(original) - 1));
        for (int i = 0; i < 200 && !got_packet; ++i) {
            const int rc = queue.receive([&](const QueuedPacket& packet) {
                captured_size = packet.payload.size();
                got_packet = contains_bytes(packet.payload, original);
                std::vector<uint8_t> replacement(packet.payload.begin(), packet.payload.end());
                const char* changed = "nfq-new";
                if (got_packet && replace_live && rewrite_udp_payload(replacement, changed)) {
                    replacement_checksum_valid = valid_udp_checksum(replacement);
                    replacement_ip_checksum_valid = valid_ip_checksum(replacement);
                    replacement_checksum = replacement.size() >= 28
                                              ? read_u16_be(replacement.data() + 26)
                                              : 0;
                    original_checksum = packet.payload.size() >= 28
                                            ? read_u16_be(packet.payload.data() + 26)
                                            : 0;
                    if (env_enabled("KPBR_NFQUEUE_DIAGNOSTIC_ACK")) {
                        int kernel_errno = 0;
                        replacement_sent = queue.verdict_checked_for_test(
                            packet.packet_id, NF_ACCEPT, replacement.data(), replacement.size(),
                            1000, kernel_errno);
                        INFO("NFQUEUE replacement ACK errno=" << kernel_errno);
                    } else {
                        replacement_sent = queue.verdict(packet.packet_id, NF_ACCEPT,
                                                         replacement.data(), replacement.size());
                    }
                } else if (got_packet) {
                    if (env_enabled("KPBR_NFQUEUE_DIAGNOSTIC_ACK")) {
                        int kernel_errno = 0;
                        replacement_sent = queue.verdict_checked_for_test(
                            packet.packet_id, NF_ACCEPT, nullptr, 0, 1000, kernel_errno);
                        INFO("NFQUEUE ACCEPT ACK errno=" << kernel_errno);
                    } else {
                        replacement_sent = queue.verdict(packet.packet_id, NF_ACCEPT);
                    }
                }
            });
            REQUIRE(rc >= 0);
            if (!got_packet) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(got_packet);
        REQUIRE(replacement_sent);
        INFO("captured IPv4 packet bytes=" << captured_size << " replacement checksum valid="
                                            << replacement_checksum_valid << " replacement IP="
                                            << replacement_ip_checksum_valid << " original checksum="
                                            << original_checksum << " replacement checksum="
                                            << replacement_checksum);
        std::string received;
        REQUIRE(receive_udp(server, received));
        CHECK(received == (replace_live ? "nfq-new" : "nfq-old"));
    } catch (const std::exception& error) {
        INFO(error.what());
        CHECK(false);
    }
    CHECK(shell(cleanup) == 0);
    ::close(client);
    ::close(server);
}

TEST_CASE("nfqueue: absent listener bypasses queue in isolated netns") {
    if (!live_wrappers_enabled()) return;
    constexpr uint16_t port = 39054;
    constexpr const char* rule =
        "iptables -I OUTPUT 1 -p udp --dport 39054 -j NFQUEUE --queue-num 9054 --queue-bypass";
    constexpr const char* cleanup =
        "iptables -D OUTPUT -p udp --dport 39054 -j NFQUEUE --queue-num 9054 --queue-bypass";
    const int server = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    const int client = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(server >= 0);
    REQUIRE(client >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(shell(rule) == 0);
    const char marker[] = "nfq-bypass";
    REQUIRE(::sendto(client, marker, sizeof(marker) - 1, 0,
                     reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
            static_cast<ssize_t>(sizeof(marker) - 1));
    std::string received;
    REQUIRE(receive_udp(server, received));
    CHECK(received == marker);
    CHECK(shell(cleanup) == 0);
    ::close(client);
    ::close(server);
}

TEST_CASE("nfqueue: queue overflow fail-opens while one packet is held") {
    if (!live_wrappers_enabled() || !env_enabled("KPBR_NFQUEUE_OVERFLOW_LIVE")) return;
    constexpr uint16_t port = 39061;
    constexpr uint16_t queue_number = 9055;
    constexpr int packet_count = 10;
    constexpr const char* rule =
        "iptables -t mangle -I POSTROUTING 1 -p udp --dport 39061 -j NFQUEUE --queue-num 9055";
    constexpr const char* cleanup =
        "iptables -t mangle -D POSTROUTING -p udp --dport 39061 -j NFQUEUE --queue-num 9055";
    int server = -1;
    int client = -1;
    try {
        server = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        client = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        REQUIRE(server >= 0);
        REQUIRE(client >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        REQUIRE(shell(rule) == 0);
        NfQueueOptions options;
        options.queue_num = queue_number;
        options.queue_maxlen = 1;
        options.fail_open = true;
        NfQueue queue(options);
        CHECK(queue.receive([](const QueuedPacket&) {}) == 0);
        for (int i = 0; i < packet_count; ++i) {
            const std::string marker = "nfq-overflow-" + std::to_string(i);
            REQUIRE(::sendto(client, marker.data(), marker.size(), 0,
                             reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                    static_cast<ssize_t>(marker.size()));
        }
        uint32_t held_id = 0;
        bool held = false;
        for (int i = 0; i < 100 && !held; ++i) {
            const int rc = queue.receive([&](const QueuedPacket& packet) {
                if (!held) {
                    held_id = packet.packet_id;
                    held = true;
                }
            });
            REQUIRE(rc >= 0);
            if (!held) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(held);
        const int passed_while_held = drain_udp(server, 400);
        INFO("NFQUEUE overflow packets passed while held=" << passed_while_held
                                                             << " overruns=" << queue.overruns());
        CHECK(passed_while_held >= packet_count - 1);
        REQUIRE(queue.verdict(held_id, NF_ACCEPT));
        CHECK(passed_while_held + drain_udp(server, 400) == packet_count);
        CHECK(shell(cleanup) == 0);
    } catch (const std::exception& error) {
        INFO(error.what());
        CHECK(false);
        (void)shell(cleanup);
    }
    if (client >= 0) ::close(client);
    if (server >= 0) ::close(server);
}

TEST_CASE("nflog: live packet copy uses scoped group (isolated netns only)") {
    if (!live_wrappers_enabled() || !env_enabled("KPBR_NFLOG_LIVE")) return;
    constexpr uint16_t port = 39055;
    constexpr const char* rule =
        "iptables -I OUTPUT 1 -p udp --dport 39055 -j NFLOG --nflog-group 9054 --nflog-size 2048 --nflog-threshold 1";
    constexpr const char* cleanup =
        "iptables -D OUTPUT -p udp --dport 39055 -j NFLOG --nflog-group 9054 --nflog-size 2048 --nflog-threshold 1";
    try {
        NfLog log;
        CHECK(log.receive([](const LoggedPacket&) {}) == 0);
        REQUIRE(shell(rule) == 0);
        const int server = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        const int client = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        REQUIRE(server >= 0);
        REQUIRE(client >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        const char marker[] = "nflog-marker";
        REQUIRE(::sendto(client, marker, sizeof(marker) - 1, 0,
                         reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                static_cast<ssize_t>(sizeof(marker) - 1));
        bool copied = false;
        for (int i = 0; i < 200 && !copied; ++i) {
            const int rc = log.receive([&](const LoggedPacket& packet) {
                copied = contains_bytes(packet.payload, marker);
            });
            REQUIRE(rc >= 0);
            if (!copied) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        INFO("NFLOG malformed=" << log.malformed_packets() << " overruns=" << log.overruns()
                                  << " errno=" << log.last_errno());
        REQUIRE(copied);
        ::close(client);
        ::close(server);
        CHECK(shell(cleanup) == 0);
    } catch (const std::exception& error) {
        INFO(error.what());
        CHECK(false);
        (void)shell(cleanup);
    }
}

TEST_CASE("conntrack: live destination-specific IPv4 and IPv6 deletion (isolated netns only)") {
    if (!live_wrappers_enabled() || !env_enabled("KPBR_CONNTRACK_LIVE")) return;
    constexpr const char* v4_output =
        "iptables -I OUTPUT 1 -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v4_input =
        "iptables -I INPUT 1 -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v6_output =
        "ip6tables -I OUTPUT 1 -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v6_input =
        "ip6tables -I INPUT 1 -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v4_output_del =
        "iptables -D OUTPUT -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v4_input_del =
        "iptables -D INPUT -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v6_output_del =
        "ip6tables -D OUTPUT -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* v6_input_del =
        "ip6tables -D INPUT -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT";
    constexpr const char* mark_rule =
        "iptables -t mangle -I OUTPUT 1 -p udp --dport 39057 -m conntrack --ctstate NEW -j CONNMARK --set-mark 0x40000000/0xffffffff";
    constexpr const char* mark_rule_del =
        "iptables -t mangle -D OUTPUT -p udp --dport 39057 -m conntrack --ctstate NEW -j CONNMARK --set-mark 0x40000000/0xffffffff";
    constexpr const char* zone_rule =
        "iptables -t raw -I OUTPUT 1 -p udp --dport 39056 -j CT --zone 7";
    constexpr const char* zone_rule_del =
        "iptables -t raw -D OUTPUT -p udp --dport 39056 -j CT --zone 7";
    bool mark_active = false;
    bool zone_active = false;
    try {
        REQUIRE(shell(v4_output) == 0);
        REQUIRE(shell(v4_input) == 0);
        REQUIRE(shell(v6_output) == 0);
        REQUIRE(shell(v6_input) == 0);
        mark_active = shell(mark_rule) == 0;
        zone_active = shell(zone_rule) == 0;
        INFO("conntrack live mark rule=" << mark_active << " zone rule=" << zone_active);
        UdpFlow v4_target = make_udp_flow(AF_INET, 39056);
        UdpFlow v4_keep = make_udp_flow(AF_INET, 39057);
        TcpFlow tcp_target = make_tcp_flow(39059);
        std::string ignored;
        REQUIRE(receive_udp(v4_target.server, ignored));
        REQUIRE(receive_udp(v4_keep.server, ignored));
        auto entries = dump_entries(ConntrackFamily::ipv4);
        const ConntrackEntry* target = find_udp(entries, ConntrackFamily::ipv4,
                                                v4_target.source_port, v4_target.destination_port);
        const ConntrackEntry* keep = find_udp(entries, ConntrackFamily::ipv4,
                                              v4_keep.source_port, v4_keep.destination_port);
        const ConntrackEntry* tcp = find_protocol(entries, ConntrackFamily::ipv4, IPPROTO_TCP,
                                                  tcp_target.source_port, tcp_target.destination_port);
        REQUIRE(target != nullptr);
        REQUIRE(keep != nullptr);
        REQUIRE(tcp != nullptr);
        if (mark_active) CHECK(keep->mark == 0x40000000);
        if (zone_active) {
            CHECK(target->original.has_zone);
            if (target->original.has_zone) CHECK(target->original.zone == 7);
        }
        std::vector<ConntrackEntry> deletes{*target};
        ConntrackDeleteBatch batch(deletes);
        for (int i = 0; i < 50 && !batch.complete(); ++i) {
            const int rc = batch.receive(100);
            INFO("v4 target delete rc=" << rc << " errno=" << batch.last_errno()
                                         << " failures=" << batch.failures());
            REQUIRE(rc >= 0);
        }
        REQUIRE(batch.complete());
        CHECK(batch.failures() == 0);
        auto after = dump_entries(ConntrackFamily::ipv4);
        CHECK(find_udp(after, ConntrackFamily::ipv4, v4_target.source_port,
                       v4_target.destination_port) == nullptr);
        CHECK(find_udp(after, ConntrackFamily::ipv4, v4_keep.source_port,
                       v4_keep.destination_port) != nullptr);
        if (mark_active) {
            ConntrackOptions skip_owned_mark;
            skip_owned_mark.filter = [](const ConntrackEntry& entry) {
                return entry.mark != 0x40000000;
            };
            ConntrackDump filtered(ConntrackFamily::ipv4, skip_owned_mark);
            for (int i = 0; i < 50 && !filtered.complete(); ++i)
                REQUIRE(filtered.receive(100) >= 0);
            REQUIRE(filtered.complete());
            CHECK(find_udp(filtered.entries(), ConntrackFamily::ipv4, v4_keep.source_port,
                           v4_keep.destination_port) == nullptr);
        }
        std::vector<ConntrackEntry> tcp_deletes{*tcp};
        ConntrackDeleteBatch tcp_batch(tcp_deletes);
        for (int i = 0; i < 50 && !tcp_batch.complete(); ++i) REQUIRE(tcp_batch.receive(100) >= 0);
        REQUIRE(tcp_batch.complete());
        CHECK(tcp_batch.failures() == 0);
        close_flow(v4_target);
        close_flow(v4_keep);
        close_flow(tcp_target);

        int ipv6_probe = ::socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (ipv6_probe >= 0) {
            ::close(ipv6_probe);
            UdpFlow v6 = make_udp_flow(AF_INET6, 39058);
            REQUIRE(receive_udp(v6.server, ignored));
            TcpFlow v6_tcp = make_tcp_flow(AF_INET6, 39060);
            auto v6_entries = dump_entries(ConntrackFamily::ipv6);
            const ConntrackEntry* v6_target = find_udp(v6_entries, ConntrackFamily::ipv6,
                                                       v6.source_port, v6.destination_port);
            const ConntrackEntry* v6_tcp_target = find_protocol(
                v6_entries, ConntrackFamily::ipv6, IPPROTO_TCP, v6_tcp.source_port,
                v6_tcp.destination_port);
            REQUIRE(v6_target != nullptr);
            REQUIRE(v6_tcp_target != nullptr);
            std::vector<ConntrackEntry> v6_deletes{*v6_target};
            ConntrackDeleteBatch v6_batch(v6_deletes);
            for (int i = 0; i < 50 && !v6_batch.complete(); ++i) REQUIRE(v6_batch.receive(100) >= 0);
            REQUIRE(v6_batch.complete());
            CHECK(v6_batch.failures() == 0);
            std::vector<ConntrackEntry> v6_tcp_deletes{*v6_tcp_target};
            ConntrackDeleteBatch v6_tcp_batch(v6_tcp_deletes);
            for (int i = 0; i < 50 && !v6_tcp_batch.complete(); ++i)
                REQUIRE(v6_tcp_batch.receive(100) >= 0);
            REQUIRE(v6_tcp_batch.complete());
            CHECK(v6_tcp_batch.failures() == 0);
            close_flow(v6);
            close_flow(v6_tcp);
        }
        CHECK(shell(v4_output_del) == 0);
        CHECK(shell(v4_input_del) == 0);
        CHECK(shell(v6_output_del) == 0);
        CHECK(shell(v6_input_del) == 0);
        if (mark_active) CHECK(shell(mark_rule_del) == 0);
        if (zone_active) CHECK(shell(zone_rule_del) == 0);
    } catch (const std::exception& error) {
        INFO(error.what());
        CHECK(false);
        (void)shell(v4_output_del);
        (void)shell(v4_input_del);
        (void)shell(v6_output_del);
        (void)shell(v6_input_del);
        if (mark_active) (void)shell(mark_rule_del);
        if (zone_active) (void)shell(zone_rule_del);
    }
}
