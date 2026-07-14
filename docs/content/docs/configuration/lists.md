---
title: Lists
weight: 2
---

Lists are groups of sites or IP ranges that you want keen-pbr to match.

The `lists` key is an object where each key is the list name and the value is the list configuration.

List names must match `^[a-z][a-z0-9_]*$` and be at most 24 characters.

Most users start with a simple domain list such as:

```json { filename="config.json" }
{
  "lists": {
    "my_sites": {
      "domains": ["google.com", "youtube.com"]
    }
  }
}
```

## Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `url` | string | no | URL to a remote list file to download and cache |
| `domains` | array of string | no | Inline DNS-compatible domain patterns (supports a leading `*.`) |
| `ip_cidrs` | array of string | no | Inline IP addresses or CIDR ranges |
| `file` | string | no | Path to a local list file |
| `ttl_ms` | integer | no (default: `0`) | How long resolved IPs should stay cached for domain-based lists. Most users can leave this at `0`. |

Inline, local-file, and URL-backed lists use the same domain syntax. A leading
`*.` and one trailing root dot are normalized away, so `*.google.com` is emitted
to dnsmasq as `google.com`. DNS service labels containing underscores are allowed;
whitespace, directive separators, malformed wildcards, and invalid labels are skipped
in files and rejected in inline configuration.

Remote list URLs and every redirect must use HTTP or HTTPS. Private and local
HTTP(S) destinations remain allowed for router-local list deployments.

Local list sources must be regular files and may not be symlinks, devices, or
FIFOs. Local and cached files use `daemon.max_file_size_bytes` (8 MiB by default)
and each physical line is limited to 4096 bytes.

At least one of `url`, `domains`, `ip_cidrs`, or `file` must be provided. 

{{< callout type="warning" >}}
If you combine multiple sources in a single list entry, all entries are merged, but this is discouraged and may be forbidden in future releases.
{{< /callout >}}

{{% details title="Under the hood: how domain lists are matched" closed="true" %}}
Each list is backed by static and dynamic IP sets.

- Static entries from `ip_cidrs`, `file`, and `url` are loaded immediately.
- Domain entries are added later, when dnsmasq resolves them.
- If `ttl_ms` is set, those resolved IPs for domains expire automatically after that time.
{{% /details %}}

## List File Format

Whether loaded from `url` or `file`, keen-pbr expects one entry per line:

- IPv4 address: `93.184.216.34`
- IPv4 CIDR: `10.0.0.0/8`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- IPv6 CIDR: `2001:db8::/32`
- Domain: `example.com` — matches the domain and all its subdomains (dnsmasq `server=/example.com/` semantics)
  - Wildcard domain: `*.example.org` is equivalent to `example.org`, so **there is no need** to add `*.` before domain. For compatibility, the `*.` prefix is stripped automatically
- Comments: lines starting with `#` are ignored
- Empty lines are ignored

## Examples

### Remote URL list

```json { filename="config.json" }
{
  "lists": {
    "remote_list": {
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/apple"
    }
  }
}
```

Downloaded files are cached in `daemon.cache_dir`. If the URL is unreachable at startup, the cached copy is used.

{{< callout type="warning" >}}
To avoid downloading the same list again and again even when it has not changed, the remote server must support `If-Modified-Since` or `ETag`.
{{< /callout >}}

### Inline domain list

```json { filename="config.json" }
{
  "lists": {
    "my_domains": {
      "domains": ["example.com", "other.org"]
    }
  }
}
```

Both entries match the domain and all its subdomains. Writing `*.example.com` is equivalent to `example.com` — the `*.` prefix is stripped automatically by keen-pbr.

### Inline IP list

```json { filename="config.json" }
{
  "lists": {
    "my_ips": {
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

```json { filename="config.json" }
{
  "lists": {
    "local_list": {
      "file": "/etc/keen-pbr/my-list.txt"
    }
  }
}
```

All four sources are merged into a single list. `ttl_ms: 86400000` sets a 24-hour TTL for dnsmasq-resolved IPs in the dynamic set (`kpbr4d_combined` / `kpbr6d_combined`). Static IPs from `ip_cidrs` and the cached URL/file are loaded into the permanent static set (`kpbr4_combined` / `kpbr6_combined`) and are never expired automatically.
