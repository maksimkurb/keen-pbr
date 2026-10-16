# FAQ

## What firewall backend will be used?

With `daemon.firewall_backend: "auto"` (the default), keen-pbr uses nftables if the `nft` command is installed, and iptables/ipset otherwise.

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

keen-pbr uses the cached copy from `daemon.cache_dir` if available. If there is no cache either, a list that has only `url` stays empty until a download succeeds; keen-pbr keeps retrying in the background.

## How does the urltest outbound select the best link?

Every `interval_ms`, keen-pbr sends an HTTP request to the configured `url` through each link and measures the round-trip latency. With `strategy: "priority"`, the link with the lowest latency is selected. If the new best link is faster than the current one by less than `tolerance_ms`, keen-pbr does not switch. Links are checked group by group: the next group in `outbound_groups` is used only when no link in the previous one works.

If a link fails `failure_threshold` checks in a row, keen-pbr stops using it for `timeout_ms`, then tries it again with a few checks. This keeps traffic from bouncing between links while one of them is unstable.

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
- {{< term "CIDR" >}}: `10.0.0.0/8`
- Domain: `example.com` (automatically includes all sub-domains)
  - entries like `*.example.com` are also supported and handled the same way as `example.com`
- Lines starting with `#` are comments and are ignored
- Empty lines are ignored

## How do I verify routing is correctly applied?

Run:

```bash {filename="bash"}
keen-pbr status
```

keen-pbr compares the firewall rules, routing tables and `ip rule` entries in the kernel with what it expects and marks each item `OK`, `MISSING` or `MISMATCH`. The same check is on the status page of the web UI and in `GET /api/health/routing`. To check a specific site, use `keen-pbr test-routing <domain-or-ip>`.

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
The web UI can combine only `ip_cidrs` and `domains`. Combining them with `url` or `file` is not supported in the web UI and is not recommended; it may be disallowed in a future version.
{{< /callout >}}

