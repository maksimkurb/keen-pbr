#pragma once

// Compatibility layer for building against old Linux UAPI headers (the Keenetic Entware
// toolchain ships 3.4.x headers while the routers run 4.9+). All numeric values are the
// stable upstream ABI values; newer headers take precedence where they define the symbol.

#include <netinet/in.h>  // old <linux/netfilter.h> needs in_addr/in6_addr first
#include <sys/ioctl.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netlink.h>

#include <cstddef>
#include <cstdint>

// --- Macros that old headers lack (safe to test with #ifndef) -----------------------------
#ifndef NFNL_MSG_BATCH_BEGIN
#define NFNL_MSG_BATCH_BEGIN NLMSG_MIN_TYPE
#endif
#ifndef NFNL_MSG_BATCH_END
#define NFNL_MSG_BATCH_END (NLMSG_MIN_TYPE + 1)
#endif
#ifndef NFNL_SUBSYS_NFTABLES
#define NFNL_SUBSYS_NFTABLES 10
#endif
#ifndef NFPROTO_INET
#define NFPROTO_INET 1
#endif
#ifndef IPSET_PROTOCOL_MIN
#define IPSET_PROTOCOL_MIN 6  // oldest ipset protocol we speak (3.4 headers: IPSET_PROTOCOL 6)
#endif
#ifndef NS_GET_USERNS
#define NS_GET_USERNS _IO(0xb7, 0x1)  // linux/nsfs.h (4.9+); older kernels fail with ENOTTY
#endif
#ifndef NETLINK_NO_ENOBUFS
#define NETLINK_NO_ENOBUFS 5
#endif
#ifndef NETLINK_CAP_ACK
#define NETLINK_CAP_ACK 10
#endif
#ifndef NETLINK_EXT_ACK
#define NETLINK_EXT_ACK 11
#endif

// --- nf_tables: header does not exist in 3.4 ------------------------------------------------
#if __has_include(<linux/netfilter/nf_tables.h>)
#include <linux/netfilter/nf_tables.h>
#else
// Only the subset used by set_writer.cpp, with upstream values.
enum nf_tables_msg_types {
    NFT_MSG_NEWSETELEM = 12,
    NFT_MSG_GETSETELEM = 13,
    NFT_MSG_DELSETELEM = 14,
};
enum nft_set_elem_list_attributes {
    NFTA_SET_ELEM_LIST_TABLE = 1,
    NFTA_SET_ELEM_LIST_SET = 2,
    NFTA_SET_ELEM_LIST_ELEMENTS = 3,
};
enum nft_list_attributes {
    NFTA_LIST_ELEM = 1,
};
enum nft_set_elem_attributes {
    NFTA_SET_ELEM_KEY = 1,
    NFTA_SET_ELEM_TIMEOUT = 4,
    NFTA_SET_ELEM_EXPIRATION = 5,
};
enum nft_data_attributes {
    NFTA_DATA_VALUE = 1,
};
#endif

namespace keen_pbr3::nfnl::uapi {

// Enum members (cannot be probed with #ifndef) absent from old headers. Always use these
// instead of the raw enum names.
inline constexpr uint16_t kNfqaCfgMask = 4;        // NFQA_CFG_MASK
inline constexpr uint16_t kNfqaCfgFlags = 5;       // NFQA_CFG_FLAGS
inline constexpr uint32_t kNfqaCfgFFailOpen = 1;   // NFQA_CFG_F_FAIL_OPEN (1 << 0)
inline constexpr uint32_t kNfqaCfgFGso = 4;        // NFQA_CFG_F_GSO (1 << 2), Linux 3.10
inline constexpr uint16_t kNfqaCapLen = 13;        // NFQA_CAP_LEN
inline constexpr uint16_t kNfqaSkbInfo = 14;       // NFQA_SKB_INFO
inline constexpr uint32_t kNfqaSkbCsumNotReady = 1;  // NFQA_SKB_CSUMNOTREADY (1 << 0)
inline constexpr uint32_t kNfqaSkbGso = 2;         // NFQA_SKB_GSO (1 << 1)
// Attribute table size covering every NFQA_* type above, whatever NFQA_MAX the headers say.
inline constexpr std::size_t kNfqaAttrCount = 16;
inline constexpr uint16_t kNfulaCt = 18;           // NFULA_CT
inline constexpr uint16_t kCtaTupleZone = 3;       // CTA_TUPLE_ZONE
inline constexpr uint16_t kCtaZone = 18;           // CTA_ZONE
inline constexpr uint16_t kCtaFilter = 25;         // CTA_FILTER (dump filter, newer kernels only)
inline constexpr uint16_t kCtaFilterOrigFlags = 1; // CTA_FILTER_ORIG_FLAGS
inline constexpr uint32_t kCtaFilterFlagIpSrc = 1; // CTA_FILTER_FLAG_CTA_IP_SRC (libnetfilter_conntrack)

// Newer headers (linux/nsfs.h appeared in 4.9, later than every symbol above) must agree.
#if __has_include(<linux/nsfs.h>)
static_assert(kNfqaCfgMask == NFQA_CFG_MASK, "NFQA_CFG_MASK mismatch");
static_assert(kNfqaCfgFlags == NFQA_CFG_FLAGS, "NFQA_CFG_FLAGS mismatch");
static_assert(kNfqaCfgFFailOpen == NFQA_CFG_F_FAIL_OPEN, "NFQA_CFG_F_FAIL_OPEN mismatch");
static_assert(kNfqaCfgFGso == NFQA_CFG_F_GSO, "NFQA_CFG_F_GSO mismatch");
static_assert(kNfqaCapLen == NFQA_CAP_LEN, "NFQA_CAP_LEN mismatch");
static_assert(kNfqaSkbInfo == NFQA_SKB_INFO, "NFQA_SKB_INFO mismatch");
static_assert(kNfqaSkbCsumNotReady == NFQA_SKB_CSUMNOTREADY, "NFQA_SKB_CSUMNOTREADY mismatch");
static_assert(kNfqaSkbGso == NFQA_SKB_GSO, "NFQA_SKB_GSO mismatch");
static_assert(kNfulaCt == NFULA_CT, "NFULA_CT mismatch");
static_assert(kCtaTupleZone == CTA_TUPLE_ZONE, "CTA_TUPLE_ZONE mismatch");
static_assert(kCtaZone == CTA_ZONE, "CTA_ZONE mismatch");
static_assert(kCtaFilter == CTA_FILTER, "CTA_FILTER mismatch");
static_assert(kCtaFilterOrigFlags == CTA_FILTER_ORIG_FLAGS, "CTA_FILTER_ORIG_FLAGS mismatch");
#endif

}  // namespace keen_pbr3::nfnl::uapi
