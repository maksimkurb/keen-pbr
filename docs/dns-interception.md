# DNS and L7 interception

keen-pbr can learn the addresses behind domain lists from traffic that already
passes through the router. This is the only way domain lists are turned
into dynamic-set entries: keen-pbr does not configure, reload or require any
resolver (dnsmasq, the Keenetic DNS proxy or another), and does not replace the
system resolver.

The implementation has two independent parts:

* DNS response hold uses NFQUEUE. A response is parsed, matching A/AAAA
  addresses are published to the list's dynamic sets with a bounded TTL, and
  the original packet is accepted. Only the UDP marker response is rewritten;
  TCP marker responses are accepted unchanged.
* L7 sniffing uses NFLOG. The first visible payload of a flow is inspected for
  TLS SNI, HTTP Host, or a QUIC Initial name. A match updates the same dynamic
  sets and may request best-effort conntrack cleanup so a newly learned address
  is classified on the next connection.

The firewall plan owns the hooks and their order. DNS hold is attached to
postrouting for UDP/TCP DNS traffic; L7 logging is attached to the forwarding
and locally generated paths. The iptables and nftables backends use the same
logical lowering and verify the application-owned chains after apply.

### Router-originated traffic

`iproute.process_router_traffic` (default `false`) decides whether the router's
own traffic is observed. With `false`:

* L7 sniffing covers forwarded LAN traffic only; the locally generated (OUTPUT)
  sniff rules and the nft `sniff_out` chain are not planned.
* The DNS hold skips replies sent through loopback (`oifname != "lo"`, iptables
  `! -o lo`), which are the answers of a local resolver to router-local
  processes. Replies to LAN clients (including those a local resolver sends out
  through a LAN interface) are still held and learned.

With `true` both apply to the router like to a LAN client. DNS detour rules are
OUTPUT-only in both modes and are not affected by this option.

### Which clients are learned

Learning writes router-wide routing sets, so it is scoped by the interface of
the **client**, whatever resolver answered (answers of any DNS resolver are
learned):

* `route.inbound_interfaces` non-empty: only clients on those interfaces are
  learned. A VPN or guest segment that is not listed is not learned.
* `route.inbound_interfaces` empty: clients on any interface except the
  outbound/WAN ones are learned, i.e. never traffic that arrived on an
  interface used by a configured interface outbound or on the interface of a
  main-table default route. A packet coming from the internet toward a
  port-forwarded or IPv6 LAN address is therefore not parsed.

L7 sniffing looks at the request direction only (`ct direction original`) and
matches the input interface (`iifname`); the DNS hold matches the output
interface of the reply going back to the client (`oifname`). Router-originated
traffic has no input interface and follows `process_router_traffic` alone (with
`true` and an allowlist, replies over `lo` are held as well). The WAN set is
recomputed from the configuration and the main routing table on every firewall
apply, and a change of the default route triggers a runtime refresh while
interception is enabled and no allowlist is set.

## Minimal configuration

Interception is enabled by default when the required kernel facilities are
available. The effective defaults are:

```json
{
  "daemon": {
    "clear_dynamic_sets_on_apply": false
  },
  "intercept": {
    "enabled": true,
    "min_ttl_s": 300,
    "max_ttl_s": 86400,
    "dns": {
      "enabled": true,
      "queue_num": 9053,
      "hold_timeout_ms": 30,
      "marker": {
        "domain": "check.keen.pbr",
        "answer_ipv4": "127.0.0.88"
      }
    },
    "l7": {
      "enabled": true,
      "nflog_group": 9054,
      "tls": true,
      "http": true,
      "quic": true
    }
  }
}
```

`clear_dynamic_sets_on_apply` is false by default. Learned addresses therefore
survive an ordinary apply or runtime refresh and leave the sets when their
element timeout expires. A full clear is an explicit configuration choice.

`min_ttl_s` and `max_ttl_s` clamp the TTL used for addresses learned from DNS.
When a list has `ttl_ms >= 1000`, its value in seconds overrides the global
minimum for that list; the global maximum still applies. Static list entries
are not affected. L7-derived addresses have no DNS record TTL, so they use the
same list/global floor and maximum. The list-level `ttl_ms` also remains
relevant to the sets filled by interception.

## Removed: dnsmasq integration

Earlier versions could manage dnsmasq (`dns.resolver_integration`,
`dns.system_resolver`, `dns.rules`, `dns.fallback`, `ipset=`/`nftset=`
fallback). That integration has been removed. The fields are still accepted in
`config.json` but ignored, with a warning in the log; the package upgrade
cleans up the dnsmasq configuration it used to touch (see the upgrade notes in
the user documentation). The old `dns.dns_test_server` option is deprecated and
ignored as well.

## DNS packet flow

1. The firewall sends UDP or TCP DNS responses to the configured NFQUEUE.
2. keen-pbr validates the IP/UDP/TCP and DNS wire layout, matches names against
   the published `DomainIndex`, and writes matching IPv4/IPv6 addresses to the
   owned dynamic sets.
3. The packet is accepted. DNS is not proxied and the answer is not changed,
   except for the marker response described below.

TCP DNS is parsed when the complete DNS message is present in one segment. A
partial TCP message is accepted and counted as `dns_tcp_partial`; it is not
silently treated as a complete answer. Malformed packets are accepted with a
parse-error counter so interception cannot become a packet-drop policy.

The hold timeout is a userspace processing budget. It is not a kernel promise
that a packet cannot wait longer than 30 ms. A bound but frozen listener can
leave packets queued; queue-bypass does not turn that state into a timeout.
This path does not request `SO_TIMESTAMP` or another enqueue timestamp, so the
deadline starts when userspace wakes for the queue; it cannot account for time
already spent waiting in the kernel queue.

The deadline bounds only how long the answer is held, never whether the set
write happens. When it passes (for example because the answer queued behind a
slow set write) or the on-time write times out, the verdict (ACCEPT) is sent
immediately, so the answer is released at the deadline, and the unwritten adds
are queued in a fixed pending-late batch of 512 elements (reserved once, no
per-packet allocation for the elements). Once per hot-loop iteration, after the
NFQUEUE socket was read, the daemon flushes that batch with one combined set
write and a 500 ms budget for the whole flush, then requests conntrack cleanup
(stale flows to the answer addresses are reset) for added elements and
publishes the deferred events. If a flush ends with a timeout, flushes during
the next 1 s use a reduced 50 ms budget, so a stuck kernel cannot add 500 ms to
every loop iteration; the elements that fail then are counted in
`dns_late_write_errors`. If the batch is full, the overflowing adds are dropped
and counted in `dns_late_write_errors` (and `set_errors`). Admission and the
snapshot are checked per flush: adds for a replaced snapshot are dropped as
errors.

A timeout (`ETIMEDOUT`) is treated as "unknown", not as an error: the kernel
may still have applied the batch, so the unconfirmed elements are written again
in the late flush (an existing element is reported as refreshed) and only a
real kernel error counts as `set_errors`. Affected events have
`timed_out: true` and `late_write: true`, and the counters `dns_hold_timeouts`,
`dns_late_writes` and `dns_late_write_errors` are updated. The flush runs on the
hot thread, so packets read after it wait for it (at most 500 ms, normally far
less).

### What blocks the verdict: the set element cache

The verdict only waits for addresses that are not in the set yet. The daemon
remembers every element it wrote (set, address family, address, expiry and write
time) in a fixed-size in-memory cache (16384 entries, about 640 KiB, allocated
once; one mutex shared by the DNS thread and the L7 worker). For each matched
response the addresses are classified before anything is written:

* Not cached (or expiring within 2 s): written before the verdict with a single
  exclusive add. If the kernel reports that the element already exists, it is
  not an error: the element moves to the post-verdict refresh list.
* Cached and trusted: nothing is written before the verdict. A timeout refresh
  is queued for after the verdict only when the remaining lifetime is less than
  half of the desired timeout (i.e., remaining_ms < desired_timeout_s * 500).
  Elements not needing refresh are counted in `refresh_skipped` (event field
  `refresh_skipped`). Elements queued for refresh are counted in
  `dns_refresh_deferred` (event field `deferred_refresh`).
* Cached but older than 5 minutes (trust bound): the element is rewritten after
  the verdict, which also recreates an element removed behind the daemon's back.

When nothing is left to write before the verdict, it is sent immediately and
`hold_us` is just the parse time (event field `cache_hits` counts the skipped
addresses). Post-verdict refreshes ride on the same pending batch as late writes
(capacity 512, flushed once per loop iteration). If that batch is full the
refresh is dropped and counted in `refresh_dropped`; this is not an error, the
element is still present and a later answer refreshes it. L7 sniffing uses the
same cache: a trusted cached element is not written at all and does not trigger
a conntrack cleanup.

The cache is cleared whenever the sets may have changed: a new interception
snapshot is installed, every firewall apply (runtime refresh, `SIGUSR1`,
Keenetic `netfilter.d` re-apply, `clear_dynamic_sets_on_apply`), and any set
write that fails with `ENOENT`. A write that was already in flight when the
cache was cleared is not recorded. An element deleted outside keen-pbr (for
example with `nft delete element`) is only noticed after the trust bound, a
re-apply, or when the element's own timeout runs out. `set_cache_hits`,
`set_cache_misses` and the gauge `set_cache_entries` in `/api/health/service`
show how well the cache works. An element refreshed after the verdict is not
counted again in `set_refreshed`; `set_refreshed` counts elements found already
present when they were first written.

How an element's timeout is extended depends on the kernel. With nftables,
Linux 6.12 and newer extend the timeout of an existing element in place when a
non-exclusive `NEWSETELEM` carries a new `NFTA_SET_ELEM_TIMEOUT` and
`NFTA_SET_ELEM_EXPIRATION` (mainline commit
[`4201f3938914`](https://github.com/torvalds/linux/commit/4201f3938914d8df3c761754b9726770c4225d66)
"netfilter: nf_tables: set element timeout update support", first in v6.12).
When the `nft_timeout_update` [probe](#runtime-probes) proves that, a post-verdict
refresh of any number of elements is one nf_tables transaction. An element that
vanished in the meantime is created by the same message, so a successful
refresh is always reported as refreshed (it cannot be told apart from an
update). On older kernels, which silently keep the old expiration, and for
permanent elements (timeout 0) the refresh stays a delete plus add of the
element in one batch.

The ipset backend needs no probe and no fallback: a post-verdict refresh is one
non-exclusive `IPSET_CMD_ADD` per element (no `NLM_F_EXCL`, the equivalent of
`ipset add -exist`) with `IPSET_ATTR_TIMEOUT`, all sent in a single netlink
request, and never an exclusive probe first. The kernel maps the missing
`NLM_F_EXCL` to `IPSET_FLAG_EXIST`, and `mtype_add()` in
`net/netfilter/ipset/ip_set_hash_gen.h` then overwrites the extensions of an
existing element, including its timeout (`ip_set_timeout_set()`), or creates a
missing one. This works on every supported ipset protocol (6 and up) because the
dynamic sets are created with timeout support (`timeout 0` default,
per-element timeouts). As with nft, a refresh is always reported as refreshed.

`set_write_slow` counts set writes (on-time, late and L7) that took 20 ms or
more. Each such write is also logged at info, at most once per 10 s, as
`intercept: slow set write <us> (send <us>, ack <us>, batch <n> elems, set <name>)`:
`send` is the time in `sendto`, `ack` the rest, i.e. waiting for the kernel to
acknowledge. A large `ack` points at a kernel-side stall in the set update.

## Marker and payload replacement

The marker domain (`check.keen.pbr` by default) is answered with `127.0.0.88`
for the DNS hold path. Payload replacement is UDP-only and works over either
IPv4 or IPv6 transport; IPv4 header and UDP checksums are rebuilt as needed.
TCP marker responses are accepted without replacement. This marker is a
diagnostic signal, not a separate DNS server.

On newer kernels, NFQUEUE payload replacement requires the process's network
namespace to be owned by the initial user namespace. In a nested user
namespace the kernel can reject the mangle with `EPERM`; keen-pbr detects that
capability and skips replacement, accepting the original packet instead.
Rootless replacement is not claimed as verified. The marker path must
therefore be tested in a root-owned network namespace on such kernels.

## L7 visibility and cleanup

NFLOG observes visible first-flow payloads, not decrypted application data:

* TLS SNI, HTTP Host, and QUIC Initial names can be learned when present;
* DoH, DoT, and warm resolver caches are not DNS-hold inputs, though TLS/HTTP/
  QUIC metadata may still be visible to L7;
* encrypted-client-hello (ECH) names remain unavailable;
* SNI/Host fronting can associate an arbitrary destination address with a name,
  so enabling those sources has an inherent security trade-off.

When DNS hold and L7 interception are both enabled, L7 parsing submits matched
work to a separate writer and a bounded 256-job queue. DNS admission never
waits behind an L7 set write. Queue overflow or apply-time cancellation rejects
the L7 work and records the set error; it does not make the DNS hold path wait.
Each L7 set-write transaction has a 100 ms budget, which is a userspace writer
budget rather than a network-wide deadline.

When a new address is added, conntrack cleanup is best effort and deletes only
the connections of the client whose DNS answer or L7 packet taught the address
(original tuple client → learned address); other clients' connections to the
same address are left alone. Requests for one client within the 50 ms batching
window share one dump. The dump asks the kernel to return only that client's
flows (`CTA_FILTER`, Linux 5.10+); older kernels send the whole table, which is
then filtered in userspace, and the daemon stops asking after the first refusal.
The first connection can already be in flight and an application retry may be
required. Cleanup must not remove unrelated
zones or marked entries; failures are counted in health rather than hidden.

## Failure behavior

| Situation | Behavior |
|---|---|
| No NFQUEUE listener | A queue configured with bypass lets packets continue. |
| Queue overflow or netlink delivery failure | NFQUEUE `FAIL_OPEN`, when configured and supported by the kernel/rule, lets the affected packet continue. |
| Listener is bound but stalled/frozen | Already queued packets can remain held; there is no kernel hold deadline. |
| Graceful shutdown | Queueing rules are removed before the listener stops. Closing a queue with pending entries can drop them, so shutdown is not described as an ACCEPT guarantee. |
| NFLOG loss/overrun | The packet is not blocked; the loss/overrun counter and health state expose reduced learning. |
| NFLOG receive failure | L7 is marked degraded and the DNS NFQUEUE path continues. |
| Fatal listener failure | The service unbinds the listener and reports `running: false`. |
| Apply or snapshot publication | Admitted writes drain, queued L7 work is cancelled, and the old snapshot is invalidated before the new snapshot is published. Failed publication does not permit stale writes; health exposes initialization/degraded state. |
| NFQUEUE or NFLOG cannot be bound | Only that part is disabled before any rule references it; the other keeps running. The failure is cached until the next runtime refresh. |
| Kernel rejects `NFQA_CFG_F_FAIL_OPEN` | The queue runs without fail-open, `capabilities.fail_open` is `false` and a warning is reported. |
| Dynamic set write test fails after apply | The interception rules are detached again, DNS hold and L7 are disabled and the reason is reported. The apply itself still succeeds. |
| ctnetlink does not answer | Conntrack cleanup is disabled with a warning; interception continues. |
| DNS/L7 capability missing | The unavailable part is disabled and reported independently; the other part can remain active. |

## Kernel requirements

Release builds compile against old UAPI headers (Linux 3.4 on mips, 3.10 on
aarch64, see `src/netfilter/uapi_compat.hpp`); the routers run newer kernels
(Keenetic 4.9+). The versions below are the first mainline release in which
each primitive exists. They were taken from upstream history: every row names
the file that was checked at successive tags of
[torvalds/linux](https://github.com/torvalds/linux) (`blob/<tag>/<path>`;
headers live in `include/linux/netfilter/` before v3.7 and in
`include/uapi/linux/netfilter/` since). Vendor kernels may backport or strip
features, so keen-pbr does not trust these numbers at runtime: see
[Runtime probes](#runtime-probes).

| Feature (what keen-pbr uses it for) | First mainline | Checked in (upstream) | Needed by | If missing |
|---|---|---|---|---|
| nfnetlink_queue: `NFQNL_CFG_CMD_BIND`, `NFQA_CFG_PARAMS`, `NFQA_PAYLOAD` in a verdict | 2.6.14 | `nfnetlink_queue.h` | DNS hold | required |
| NFQUEUE handler registered at module load. keen-pbr never sends `PF_BIND`; before this the bind is ACKed but nothing is ever queued | 3.8 | `net/netfilter/nfnetlink_queue_core.c`: `PF_BIND` is a handler registration in v3.7 and `return 0` in v3.8 | DNS hold | required; **cannot be probed** (the bind succeeds) |
| `xt_NFQUEUE` target | 2.6.16 | `net/netfilter/xt_NFQUEUE.c` (`ipt_NFQUEUE` before) | DNS hold, iptables | required |
| `xt_NFQUEUE --queue-bypass` (`xt_NFQ_info_v2`) | 2.6.39 | `xt_NFQUEUE.h` | DNS hold, iptables | required (rule fails to load) |
| nft `queue ... bypass` (`NFT_QUEUE_FLAG_BYPASS`, `nft_queue.c`) | 3.14 | `nf_tables.h`, `nft_queue.c` | DNS hold, nft | required |
| `NFQA_CFG_FLAGS`/`NFQA_CFG_MASK` + `NFQA_CFG_F_FAIL_OPEN` | 3.6 | `nfnetlink_queue.h` | DNS hold | optional: runs without fail-open, `fail_open=false` + warning |
| `NFQA_CAP_LEN` (detects truncated captures) | 3.7 | `nfnetlink_queue.h` | DNS hold | optional: the IP header length check still catches truncation |
| `NFQNL_MSG_VERDICT_BATCH` (batch accept on shutdown) | 3.1 | `nfnetlink_queue.h` | shutdown drain | optional: packets are released one by one |
| Verdict payload replacement in the initial user namespace | 2.6.14 (same as `NFQA_PAYLOAD`) | project live test, see [Marker and payload replacement](#marker-and-payload-replacement); no upstream commit identified for the user-namespace limit | marker rewrite | optional: marker is accepted unchanged |
| nfnetlink_log: `NFULNL_MSG_CONFIG` bind/mode | 2.6.14 | `net/netfilter/nfnetlink_log.c` | L7 | required |
| `xt_NFLOG` target (`--nflog-group`, `--nflog-size`, `--nflog-threshold`) | 2.6.20 | `net/netfilter/xt_NFLOG.c`, `xt_NFLOG.h` | L7, iptables | required |
| nft `log group ... snaplen ... queue-threshold` (`NFTA_LOG_GROUP`, `_SNAPLEN`, `_QTHRESHOLD`) | 3.13 | `nf_tables.h`, `nft_log.c` | L7, nft | required |
| `NFULA_CT` (conntrack attributes in NFLOG records) | 4.4 | `nfnetlink_log.h` | not used | not required |
| `xt_connbytes` (`--connbytes ... packets`) | 2.6.16 | `net/netfilter/xt_connbytes.c` | L7, iptables | required |
| Runtime conntrack accounting switch (`nf_conntrack_acct`) | 2.6.27 | `net/netfilter/nf_conntrack_acct.c` | L7 | required (keen-pbr enables it) |
| `xt_conntrack` `--ctdir` (`XT_CONNTRACK_DIRECTION`) | 2.6.25 | `xt_conntrack.h` | DNS hold, iptables | required |
| nft `ct original packets` (`NFT_CT_PKTS`) | 4.5 | `nf_tables.h`, `nft_ct.c` | L7, nft | required |
| nft `ct direction` (`NFTA_CT_DIRECTION`) | 3.13 | `nf_tables.h` | DNS hold, nft | required |
| ipset netlink protocol 6 (`IPSET_CMD_PROTOCOL`), per-element `IPSET_ATTR_TIMEOUT`, `hash:net` | 2.6.39 | `ipset/ip_set.h` (`IPSET_PROTOCOL 6` in v2.6.39, v3.4 and v4.9), `ipset/ip_set_hash_net.c` | set writes, ipset | required |
| nf_tables: batch (`NFNL_MSG_BATCH_BEGIN`), `NFT_MSG_NEWSETELEM`/`DELSETELEM` | 3.13 | `nfnetlink.h`, `nf_tables.h`, `nf_tables_api.c` | set writes, nft | required |
| nf_tables `inet` family (`NFPROTO_INET`) | 3.14 | `include/uapi/linux/netfilter.h` | nft | required |
| nf_tables set element timeout (`NFT_SET_TIMEOUT`, `NFTA_SET_ELEM_TIMEOUT`) | 4.1 | `nf_tables.h` | set writes, nft | required |
| nf_tables update of an existing element's timeout by a non-exclusive `NEWSETELEM` (`NFT_TRANS_UPD_TIMEOUT`/`_EXPIRATION`) | 6.12 | `net/netfilter/nf_tables_api.c`, commit `4201f3938914` | one-transaction timeout refresh, nft | optional: refresh uses delete+add (`nft_timeout_update` probe) |
| ctnetlink dump and delete | 2.6.16 | `net/netfilter/nf_conntrack_netlink.c` | conntrack cleanup | optional: cleanup disabled |
| ctnetlink `CTA_ZONE` | 2.6.34 | `nfnetlink_conntrack.h` | conntrack cleanup | optional (zones are only read when present) |
| ctnetlink `CTA_TUPLE_ZONE` | 4.3 | `nfnetlink_conntrack.h` | conntrack cleanup | optional |
| ctnetlink kernel-side dump filter (`CTA_FILTER`) | 5.10 | `nfnetlink_conntrack.h` | conntrack cleanup | optional: full-table dump filtered in userspace |
| `NETLINK_NO_ENOBUFS` | 2.6.30 | `netlink.h` | socket tuning | optional |
| `NETLINK_CAP_ACK` | 4.3 | `netlink.h` | socket tuning | optional (`setsockopt` failure is ignored) |
| `NETLINK_EXT_ACK` | 4.12 | `netlink.h` | socket tuning | optional (`setsockopt` failure is ignored) |
| `NS_GET_USERNS` ioctl (replacement capability) | 4.9 | `include/uapi/linux/nsfs.h` | marker rewrite | optional: `payload_replacement` is `unknown` outside the initial namespace |
| nft `numgen` (balanced routing only, not interception) | 4.9 | `net/netfilter/nft_numgen.c` | `balance` outbounds, nft | outside this page |

### Minimum per backend

| Backend | DNS hold | L7 sniffing | Full feature set |
|---|---|---|---|
| iptables | 3.8 (queue handler registration; the `--queue-bypass`, ipset and `--ctdir` rows are all older) | 2.6.39 (ipset); L7 modules are older | 3.8 plus 3.6 for fail-open |
| nftables | 4.1 (set element timeout) | 4.5 (`ct original packets`) | 4.5 (4.9 for balanced outbounds) |

Fail-open needs 3.6, which every kernel that meets the iptables DNS-hold
minimum (3.8) already has; the `fail_open` probe can only fail there on vendor
kernels that stripped the feature.

Required means the part that needs it is disabled and reported when it is
missing; keen-pbr itself keeps running and the other part stays active.
Optional means the part keeps working with reduced behaviour, a warning in
`/api/health/service` and a log line.

### Kernel modules per platform

* OpenWrt: `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` and (nftables)
  `kmod-nft-queue` (plus `kmod-nf-conntrack-netlink`) are dependencies of the `keen-pbr` packages (`nft_log` is in
  `kmod-nft-core`; ctnetlink comes with `conntrack`). Without them the NFQUEUE
  and NFLOG binds fail with `EINVAL` (the nfnetlink subsystem is not
  registered); the `nfqueue`/`nflog` probes then report "kernel module
  nfnetlink_queue is not available" (decided by the absence of
  `/proc/net/netfilter/nfnetlink_queue` / `nfnetlink_log`).
* Debian and similar: the stock kernel ships these modules; nothing to install.
* Keenetic: the modules are part of the firmware.

Before binding, the daemon tries `modprobe` once for `nfnetlink_queue` and
`nfnetlink_log` (plus `nft_queue`, `nft_log`, `nft_ct` for nftables) and ignores
failures.

### Runtime probes

Version numbers say what upstream shipped, not what a given router kernel
does. After the `/proc/net/ip*_tables_*` check (iptables only), keen-pbr
therefore exercises each primitive and reports the result in
`intercept.probes` of `GET /api/health/service`. Nothing here touches user
traffic and a probe never fails a configuration apply.

| Probe | Request | Result |
|---|---|---|
| `set_backend` | ipset: `IPSET_CMD_PROTOCOL` (needs a reply of at least protocol 6). nftables: a batched `NEWSETELEM` against a set that does not exist (`ENOENT` proves nf_tables answers; `EOPNOTSUPP`/`EINVAL` means it does not) | `unsupported`/`error` disables DNS hold and L7 |
| `nfqueue` | bind of the configured queue number plus `NFQA_CFG_PARAMS`, done by the service itself | failure disables DNS hold only |
| `fail_open` | `NFQA_CFG_FLAGS(FAIL_OPEN)` on the bound queue, plus a control request with an unknown flag bit. Kernels before 3.6 ignore the attribute and ACK both, so a control that is accepted means the flag was never parsed | `unsupported`: queue runs without fail-open, `capabilities.fail_open=false`, warning |
| `payload_replacement` | `NS_GET_USERNS` owner of the network namespace | `supported`, `unsupported` or `unknown`; informational |
| `nflog` | bind of the configured group, done by the service itself | failure disables L7 only |
| `set_write` | after the firewall created the sets: add and delete of `192.0.2.255` / `2001:db8::ffff` with a 1 s timeout on one `kpbr4d_*`/`kpbr6d_*` set (ipset `ADD`/`DEL`, nft `NEWSETELEM`/`DELSETELEM`) | failure removes the interception rules again and disables DNS hold and L7; `skipped` when no dynamic set exists |
| `nft_timeout_update` | nftables only, after `set_write` succeeded: on one dynamic set, delete any leftover of `192.0.2.254` / `2001:db8::fffe`, add it with a 5 s timeout, send the in-place refresh (non-exclusive `NEWSETELEM`, timeout and expiration 300 s), read the expiration back with `GETSETELEM` and delete the element again. `ok` only when the remaining expiration is above 10 s | `unsupported`/`error`: the writer keeps the delete+add refresh; never disables anything; `skipped` without a dynamic set, `not_run` for ipset |
| `conntrack` | a ctnetlink dump request, abandoned after the first reply | failure disables conntrack cleanup only (warning) |

`kernel_release` (`uname -r`) is reported for information and is never used to
decide anything. A failed probe is cached until the next runtime refresh
(which includes `SIGUSR1`) or configuration change, so a kernel that
rejects a listener is not retried on every apply.

## Health and diagnostics

`GET /api/health/service` reports an `intercept` object. The
interception health separates `dns_hold_active` and `l7_active`, reports
`capabilities.nfqueue`, `capabilities.nflog`, and `capabilities.connbytes`
(plus `fail_open`, `payload_replacement` and `conntrack_cleanup` once probed),
lists the functional [probe results](#runtime-probes) in `probes`
(`feature`, `status`, `reason`), degraded-but-running conditions in `warnings`,
and the kernel release and ipset protocol, and exposes counters such as DNS packets/parse errors/hold timeouts, partial TCP,
NFQUEUE/NFLOG overruns, set additions/errors, set cache hits/misses/entries,
deferred and dropped refreshes, and conntrack requests/deletes.
`SIGUSR1` schedules a runtime refresh and clears the cached interception
capability probe before reapplying the runtime state, so newly available kernel
facilities are rechecked.

`GET /api/dns/test` is an SSE stream, not the removed probe listener. Its
default `show=keen-pbr` view delivers marker events and closes after a matching
marker. Use `show=all` (or `show=full`) for the continuous observation stream;
it includes parsed nonmatches and emits `GAP` notices when events are lost.
Events include `source` (`dns`, `marker`, `sni`, `http`, or `quic`), the captured
`client_ip`, `domain`, matched `lists`, learned `ips`, `hold_us`, and
`timed_out`, plus add/refresh/error, `cache_hits`, `deferred_refresh` and
parse/write timing counts.

## Platform limits

The portable implementation has not verified Keenetic hardware offload,
legacy iptables 1.4.21 behavior, or a root-owned marker run on a production
router. Those require device-specific/manual checks. Absence of the
Keenetic dump is not evidence that these paths are supported.

See the [DNS configuration reference](../docs/content/docs/configuration/dns.md),
[REST API reference](../docs/content/docs/rest-api.md), and
[firewall architecture](firewall-architecture.md) for operational details.
