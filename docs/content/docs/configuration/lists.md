---
title: Lists
weight: 2
---

Lists define named collections of domains and/or IP CIDRs used in route rules and DNS rules.

The `lists` key is an object where each key is the list name and the value is the list configuration.

## Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `url` | string | — | URL to a remote list file to download and cache |
| `domains` | array of string | — | Inline domain patterns (supports `*.` prefix wildcards) |
| `ip_cidrs` | array of string | — | Inline IP addresses or CIDR ranges |
| `file` | string | — | Path to a local list file |
| `ttl_ms` | integer | `0` | TTL for dnsmasq-resolved ipset entries in milliseconds; `0` means no timeout |

At least one of `url`, `domains`, `ip_cidrs`, or `file` must be provided. Multiple sources can be combined in a single list entry — all entries are merged.

## List File Format

Whether loaded from `url` or `file`, keen-pbr3 expects one entry per line:

- IPv4 address: `93.184.216.34`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- CIDR: `10.0.0.0/8`
- Domain: `example.com`
- Wildcard domain: `*.example.org` (matches all subdomains)
- Comments: lines starting with `#` are ignored
- Empty lines are ignored

## Examples

### Remote URL list

```json
{
  "lists": {
    "remote-list": {
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/instagram"
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
      "domains": ["example.com", "*.example.org"]
    }
  }
}
```

The `*.example.org` entry matches `www.example.org`, `api.example.org`, etc.

### Inline IP list

```json
{
  "lists": {
    "my-ips": {
      "ip_cidrs": ["93.184.216.34", "10.0.0.0/8"]
    }
  }
}
```

### Local file

```json
{
  "lists": {
    "local-list": {
      "file": "./my-list.txt"
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
      "file": "./local-additions.txt",
      "ttl_ms": 86400000
    }
  }
}
```

All four sources are merged into a single list. `ttl_ms: 86400000` sets a 24-hour TTL for dnsmasq ipset entries.
