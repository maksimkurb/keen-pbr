# DNS and L7 interception

keen-pbr can learn the addresses behind domain lists from traffic that already
passes through the router. This is the primary path when
`dns.resolver_integration` is `none` (the default); it does not require
dnsmasq and does not replace the system resolver.

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

## Minimal configuration

Interception is enabled by default when the required kernel facilities are
available. The effective defaults are:

```json
{
  "daemon": {
    "clear_dynamic_sets_on_apply": false
  },
  "dns": {
    "resolver_integration": "none"
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
relevant to resolver-generated `ipset=`/`nftset=` fallback.

## Resolver integration

`dns.resolver_integration` has two modes:

* `none` (default): keen-pbr does not configure, reload, or require dnsmasq.
  DNS interception and L7 learning are independent of this setting.
* `dnsmasq`: keen-pbr manages the optional per-list upstream configuration and
  its health check. If DNS interception is unavailable on the device, the
  generated dnsmasq configuration includes the conditional `ipset=`/`nftset=`
  fallback so dnsmasq can populate dynamic sets instead.

For a pre-existing configuration with this field absent, keen-pbr migrates to
`dnsmasq` only when `dns.rules` is non-empty or `dns.system_resolver` exists;
otherwise it persists `none`. `dns.rules` with `none` is accepted but warns,
because per-list upstream selection requires dnsmasq. The old
`dns.dns_test_server` option is deprecated and ignored.

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

When a new address is added, conntrack cleanup is best effort and targets the
matching original tuple/address. The first connection can already be in flight
and an application retry may be required. Cleanup must not remove unrelated
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
| DNS/L7 capability missing | The unavailable part is disabled and reported independently; the other part can remain active. |

## Health and diagnostics

`GET /api/health/service` reports resolver mode and an `intercept` object. The
interception health separates `dns_hold_active` and `l7_active`, reports
`capabilities.nfqueue`, `capabilities.nflog`, and `capabilities.connbytes`, and
exposes counters such as DNS packets/parse errors/hold timeouts, partial TCP,
NFQUEUE/NFLOG overruns, set additions/errors, and conntrack requests/deletes.
`SIGUSR1` schedules a runtime refresh and clears the cached interception
capability probe before reapplying the runtime state, so newly available kernel
facilities are rechecked.

`GET /api/dns/test` is an SSE stream, not the removed probe listener. It emits
`HELLO` and `INTERCEPT` events with `source` (`dns`, `marker`, `sni`, `http`, or
`quic`), `domain`, matched `lists`, learned `ips`, `hold_us`, and
`timed_out`, plus add/refresh/error counts.

## Platform limits

The portable implementation has not verified Keenetic hardware offload,
legacy iptables 1.4.21 behavior, or a root-owned marker run on a production
router. Those require device-specific/manual checks. Absence of the
Keenetic dump is not evidence that these paths are supported.

See the [DNS configuration reference](../docs/content/docs/configuration/dns.md),
[REST API reference](../docs/content/docs/rest-api.md), and
[firewall architecture](firewall-architecture.md) for operational details.
