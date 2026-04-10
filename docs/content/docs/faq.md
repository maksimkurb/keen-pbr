---
title: FAQ
weight: 6
aliases:
  - /docs/troubleshooting/faq/
---

## What firewall backend will be used?

keen-pbr auto-detects the firewall backend at startup. It uses nftables if available, and falls back to iptables/ipset on older kernels.

## How do I reload lists without restarting the service?

Use `SIGHUP`:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

This triggers a full reload: it re-downloads all remote lists and re-applies firewall and routing rules.

{{< callout type="info" >}}
`SIGUSR1` is different — it re-verifies routing tables and triggers immediate URL tests, but does **not** re-download lists.
{{< /callout >}}

## What happens if a remote list URL is unreachable at startup?

keen-pbr uses the cached copy from `daemon.cache_dir` if available. If no cache exists and the URL is unreachable, the remote source contributes no entries for this run. For a URL-only list, that means the list is effectively empty and no matching set or firewall rule is created.

## How does the urltest outbound select the best child?

On each probe interval, keen-pbr sends an HTTP request to the configured `url` from each child outbound and measures the round-trip latency. The child with the lowest latency is selected. Children within `tolerance_ms` of the best are considered equivalent and selected by weight.

The circuit breaker prevents flapping: after `failure_threshold` consecutive failures, the circuit opens and that child is bypassed during the `timeout_ms` cooldown. After cooldown, it enters half-open state and gets limited probe attempts to recover.

## Can I route DNS queries through a VPN?

Yes. Use the `detour` field in `dns.servers` to bind DNS queries for that server to a specific outbound:

```json
{
  "servers": [
    {
      "tag": "vpn_dns",
      "address": "10.8.0.1",
      "detour": "vpn"
    }
  ]
}
```

## What's the list file format?

One entry per line. Supported formats:
- IPv4 address: `93.184.216.34`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- CIDR: `10.0.0.0/8`
- Domain: `example.com` (automatically includes all sub-domains)
  - entries like `*.example.com` are also supported and handled the same way as `example.com`
- Lines starting with `#` are comments and are ignored
- Empty lines are ignored

## How do I verify routing is correctly applied?

Use the routing health endpoint:

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
```

This checks the live kernel state (firewall chain, firewall rules, routing tables, policy rules) against the expected configuration and reports `ok`, `missing`, or `mismatch` for each element.

## Can I combine multiple sources in one list?

Yes. `url`, `domains`, `ip_cidrs`, and `file` can all be set in the same list entry and are merged:

```json
{
  "lists": {
    "combined": {
      "url": "https://example.com/remote.txt",
      "domains": ["extra.example.com"],
      "ip_cidrs": ["192.168.100.0/24"],
      "file": "./local.txt"
    }
  }
}
```

{{< callout type="warning" >}}
In the WebUI combining is supported only for `ip_cidrs` + `domains`. Combining them with `url` or with `file` is not supported in WebUI and discouraged; thus it can be forbidden in the future to prevent mis-understanding.
{{< /callout >}}
