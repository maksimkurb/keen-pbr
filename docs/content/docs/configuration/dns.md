---
title: DNS
weight: 4
---

Domain-based routing works without any DNS configuration: keen-pbr intercepts DNS responses and TLS/HTTP/QUIC traffic itself and fills the sets of your lists (see the [interception architecture](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md)). No resolver is touched.

Use the DNS settings below only when you want domains in a list to be resolved through a specific DNS server, usually the same VPN that will carry the matching traffic. That requires the optional **dnsmasq integration**: set `dns.resolver_integration` to `"dnsmasq"` and keen-pbr manages a dnsmasq configuration for you (per-list upstream DNS, Keenetic static entries, fallback servers, rebind exceptions). dnsmasq is not installed by the keen-pbr package any more; install it yourself when you need this.

## Configuration

```json { filename="config.json" }
{
  "dns": {
    "resolver_integration": "dnsmasq",
    "system_resolver": {
      "address": "127.0.0.1"
    },
    "servers": [...],
    "rules": [...],
    "fallback": ["google_dns", "quad9"]
  }
}
```

| Field | Type | Description |
|---|---|---|
| `resolver_integration` | string | `none` (default) or `dnsmasq`. With `none` keen-pbr never touches the system resolver; per-list `rules`/`fallback` and `system_resolver` are inactive, while server definitions (including `detour`) remain meaningful for ordinary DNS traffic. |
| `system_resolver` | object | How keen-pbr refreshes dnsmasq on the system (required for `dnsmasq`) |
| `servers` | array | DNS server definitions |
| `rules` | array | Rules mapping lists to DNS servers |
| `fallback` | array of string | Ordered DNS server tags for queries that match no rule |
| `dns_test_server` | object | Deprecated and ignored (replaced by `intercept.dns.marker`) |

## Resolver Integration

`dns.resolver_integration` selects how keen-pbr works with the system resolver.

| Value | Behaviour |
|---|---|
| `none` | Default. The daemon fills the dynamic sets by itself. dnsmasq is neither configured, restarted nor required. |
| `dnsmasq` | keen-pbr installs a `conf-script` hook into dnsmasq, generates its configuration and verifies it through the `config-hash.keen.pbr` TXT record. When interception is unavailable on the device, dnsmasq also fills the dynamic sets (`ipset=`/`nftset=` directives). |

Configs written before this option existed are migrated automatically: if `resolver_integration` is absent and the config has non-empty `dns.rules` or a `dns.system_resolver`, `dnsmasq` is used (and written explicitly on the next save); otherwise `none`. Defining `dns.rules` with `none` logs a warning because per-list upstream DNS requires `dnsmasq`.

Switching from `dnsmasq` to `none` removes the keen-pbr hook from dnsmasq and restarts it. Make sure dnsmasq (or whatever resolver you use) has upstream servers of its own afterwards.

## System Resolver

`dns.system_resolver` tells keen-pbr how to check dnsmasq state after configuration changes. It is only used (and required) with `resolver_integration: "dnsmasq"`.

On normal router package installs, you usually should not change these settings.

| Field | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Resolver address used for integration and TXT health checks, for example `"127.0.0.1"` or `"127.0.0.1:5353"` |

## DNS Test Server (deprecated)

`dns.dns_test_server` is deprecated: the option is still accepted but ignored
(a warning is logged). The built-in probe listener was replaced by the
interceptor marker `intercept.dns.marker` (default `check.keen.pbr`, answered
with `127.0.0.88`). A UDP `nslookup check.keen.pbr` marker check works only
when DNS hold and the supported marker replacement capability are active. When
the HTTP API is enabled, you can inspect the result through the Web UI/SSE
events.

## DNS interception

The `intercept` section is enabled by default when the required kernel
capabilities are available. DNS uses NFQUEUE (queue `9053`, 30 ms userspace
processing budget); L7 uses NFLOG (group `9054`) for TLS SNI, HTTP Host and QUIC
Initial metadata. The effective DNS TTL range is 300–86400 seconds. Configure
these values only when the defaults do not suit the device:

```json
{
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

The 30 ms value is a processing budget, not a kernel guarantee. A listener
that is already bound but stalled can hold queued packets; `queue-bypass` only
covers an absent listener. During shutdown keen-pbr removes queueing rules
before stopping the listener, but closing a queue with pending entries can
drop them. See [DNS and L7 interception](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md) for
the marker user-namespace requirement, counters, capability health, and other
failure semantics.

### Kernel requirements

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
| Verdict payload replacement in the initial user namespace | 2.6.14 (same as `NFQA_PAYLOAD`) | project live test (see the [interception architecture](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md)); no upstream commit identified for the user-namespace limit | marker rewrite | optional: marker is accepted unchanged |
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
| ctnetlink dump and delete | 2.6.16 | `net/netfilter/nf_conntrack_netlink.c` | conntrack cleanup | optional: cleanup disabled |
| ctnetlink `CTA_ZONE` | 2.6.34 | `nfnetlink_conntrack.h` | conntrack cleanup | optional (zones are only read when present) |
| ctnetlink `CTA_TUPLE_ZONE` | 4.3 | `nfnetlink_conntrack.h` | conntrack cleanup | optional |
| `NETLINK_NO_ENOBUFS` | 2.6.30 | `netlink.h` | socket tuning | optional |
| `NETLINK_CAP_ACK` | 4.3 | `netlink.h` | socket tuning | optional (`setsockopt` failure is ignored) |
| `NETLINK_EXT_ACK` | 4.12 | `netlink.h` | socket tuning | optional (`setsockopt` failure is ignored) |
| `NS_GET_USERNS` ioctl (replacement capability) | 4.9 | `include/uapi/linux/nsfs.h` | marker rewrite | optional: `payload_replacement` is `unknown` outside the initial namespace |
| nft `numgen` (balanced routing only, not interception) | 4.9 | `net/netfilter/nft_numgen.c` | `balance` outbounds, nft | outside this page |

#### Minimum per backend

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

#### Runtime probes

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
| `conntrack` | a ctnetlink dump request, abandoned after the first reply | failure disables conntrack cleanup only (warning) |

`kernel_release` (`uname -r`) is reported for information and is never used to
decide anything. A failed probe is cached until the next runtime refresh
(which includes `SIGUSR1`) or configuration change, so a kernel that
rejects a listener is not retried on every apply.

## DNS Servers

Each server has a tag, optional `type`, optional `address`, and optional `detour`.
DNS server `tag` values must match `^[a-z][a-z0-9_]*$`, be at most 24 characters, and must be unique.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `type` | string | no | DNS source type: `static` (default) or `keenetic` |
| `address` | string | for `static` | IP address of the DNS server, with optional port, for example `"10.8.0.1"`, `"10.8.0.1:5353"`, `"2001:4860:4860::8888"`, or `"[2001:4860:4860::8888]:5353"` |
| `detour` | string | no | Outbound to use when contacting this DNS server |

The `detour` field is useful when a DNS server must be reached through a specific connection, usually the same VPN that will carry the matching traffic.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "type": "static",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      },
      {
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ]
  }
}
```

### `type: keenetic` (built-in router DNS via RCI)

On Keenetic routers, `type: "keenetic"` tells keen-pbr to reuse the router's current built-in DNS settings automatically.

Rules and behavior:

- At most one `dns.servers` entry may use `type: "keenetic"`.
- `address` must not be set for `type: "keenetic"` (resolved from RCI).
- keen-pbr reads unscoped `dns_server = ...` entries from the **System** proxy policy.
- If unscoped encrypted upstreams (DoH/DoT) are present, all of them are used in order.
- Otherwise, all unscoped plaintext upstreams are used in order.
- `static_a` / `static_aaaa` entries from the System policy are also propagated to generated dnsmasq config.

### How `detour` works

When `detour` is set, keen-pbr makes sure DNS queries for that server leave through the selected outbound; keen-pbr would automatically create firewall rule for specified DNS IP and port. This can also affect other clients in your network that trying to contact this DNS server directly.

For example, if `vpn_dns` has `detour: "vpn"`, then the DNS requests to `vpn_dns` will also go through `vpn`.

## DNS Rules

Rules map list names to a DNS server tag. Domains from the specified lists are resolved using the specified server.

| Field | Type | Required | Description |
|---|---|---|---|
| `enabled` | boolean | no | Whether this rule is active. `false` disables it. `true`, omitted, or `null` all mean enabled. |
| `list` | array of string | yes | List names whose domains should be resolved by this server |
| `server` | string | yes | DNS server tag to use for matched domains |

```json { filename="config.json" }
{
  "dns": {
    "rules": [
      {
        "enabled": true,
        "list": ["my_domains", "remote_list"],
        "server": "vpn_dns"
      }
    ]
  }
}
```

If `enabled` is omitted or set to `null`, the DNS rule is still treated as enabled.

## dnsmasq Integration

On packaged router installs, you usually do not need to configure dnsmasq manually.

{{% details title="Manual dnsmasq integration (advanced)" closed="true" %}}
keen-pbr provides the `generate-resolver-config` subcommand that prints dnsmasq configuration to stdout.

Two resolver types are supported:

| Resolver type | Directive style | Use with |
|---|---|---|
| `dnsmasq-ipset` | `ipset=` | iptables/ipset backend |
| `dnsmasq-nftset` | `nftset=` | nftables backend |

Example dnsmasq integration:

```text
conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq-nftset
```

Restart dnsmasq after adding this line.
{{% /details %}}

## Complete Example

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      }
    ],
    "rules": [
      {
        "list": ["my_domains", "remote_list"],
        "server": "vpn_dns"
      }
    ],
    "fallback": ["google_dns", "quad9"]
  }
}
```

{{% details title="Under the hood: how domain-based routing works" closed="true" %}}
When a domain in a matched list is resolved, dnsmasq adds the resulting IP address to an internal set used by keen-pbr. Traffic to that IP can then be routed through the correct outbound.
{{% /details %}}
