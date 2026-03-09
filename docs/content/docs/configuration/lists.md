---
title: Lists
weight: 2
---

Lists define named collections of domains and/or IP CIDRs used in route rules and DNS rules.

The `lists` key is an object where each key is the list name and the value is the list configuration.

## Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `url` | string | no | URL to a remote list file to download and cache |
| `domains` | array of string | no | Inline domain patterns (supports `*.` prefix wildcards) |
| `ip_cidrs` | array of string | no | Inline IP addresses or CIDR ranges |
| `file` | string | no | Path to a local list file |
| `ttl_ms` | integer | no (default: `0`) | TTL for dnsmasq-resolved ipset entries in milliseconds; `0` means no timeout |

At least one of `url`, `domains`, `ip_cidrs`, or `file` must be provided. Multiple sources can be combined in a single list entry — all entries are merged.

## List File Format

Whether loaded from `url` or `file`, keen-pbr3 expects one entry per line:

- IPv4 address: `93.184.216.34`
- IPv4 CIDR: `10.0.0.0/8`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- IPv6 CIDR: `2001:db8::/32`
- Domain: `example.com` — matches the domain and all its subdomains (dnsmasq `server=/example.com/` semantics)
- Wildcard domain: `*.example.org` — equivalent to `example.org`; the `*.` prefix is stripped automatically
- Comments: lines starting with `#` are ignored
- Empty lines are ignored

## Examples

### Remote URL list

```json
{
  "lists": {
    "remote-list": {
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/apple"
    }
  }
}
```

Downloaded files are cached in `daemon.cache_dir`. If the URL is unreachable at startup, the cached copy is used.

### Inline domain list

```json
{
  "lists": {
    "my-domains": {
      "domains": ["example.com", "other.org"]
    }
  }
}
```

Both entries match the domain and all its subdomains. Writing `*.example.com` is equivalent to `example.com` — the `*.` prefix is stripped automatically.

### Inline IP list

```json
{
  "lists": {
    "my-ips": {
      "ip_cidrs": [
        "93.184.216.34",
        "10.0.0.0/8",
        "2606:2800:220:1:248:1893:25c8:1946",
        "2001:db8::/32"
      ]
    }
  }
}
```

### Local file

```json
{
  "lists": {
    "local-list": {
      "file": "/etc/keen-pbr3/my-list.txt"
    }
  }
}
```

### Combined sources

```json
{
  "lists": {
    "combined": {
      "url": "https://example.com/remote-list.txt",
      "domains": ["extra.example.com"],
      "ip_cidrs": ["192.168.100.0/24"],
      "file": "/etc/keen-pbr3/local-additions.txt",
      "ttl_ms": 86400000
    }
  }
}
```

All four sources are merged into a single list. `ttl_ms: 86400000` sets a 24-hour TTL for dnsmasq ipset entries.
