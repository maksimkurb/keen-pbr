# Lists

Lists are groups of sites or IP ranges whose traffic keen-pbr routes according to your rules.

The `lists` key is an object where each key is the list name and the value is the list configuration.

A list name uses lowercase Latin letters, digits and `_`, starts with a letter, and is at most 24 characters long (`^[a-z][a-z0-9_]*$`).

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
| `domains` | array of string | no | Domains written directly in the config. A leading `*.` is optional |
| `ip_cidrs` | array of string | no | IP addresses and subnets ({{< term "CIDR" >}}) written directly in the config |
| `file` | string | no | Path to a local list file |
| `ttl_ms` | integer | no (default: `0`) | Minimum time to keep the IP addresses of this list's domains, in milliseconds. If unset or below 1000, `intercept.min_ttl_ms` is used. If the {{< term "TTL" >}} in the DNS reply is longer, the address is kept longer, but no more than `intercept.max_ttl_ms` |

Domains are written the same way in every source. `*.google.com` and `google.com` mean the same thing. Invalid domains are skipped in files and URL lists, and cause a configuration error in `domains`.

URL lists are downloaded only over HTTP or HTTPS, including from addresses on your local network.

A local file must be a regular file (not a symlink). File size is limited by `daemon.max_file_size_bytes` (8 MiB by default), and line length by 4096 bytes.

At least one of `url`, `domains`, `ip_cidrs`, or `file` must be provided. 

{{< callout type="warning" >}}
If you combine multiple sources in a single list entry, all entries are merged, but this is discouraged and may be forbidden in future releases.
{{< /callout >}}

{{% details title="Under the hood: how domain lists are matched" closed="true" %}}
- IP addresses and subnets from `ip_cidrs`, `file` and `url` are loaded right at startup.
- keen-pbr learns domain IPs later, when a device on the network connects to the site (from the DNS reply or from the connection itself).
- A domain's IP address is kept as long as the DNS reply says, but no less than the list's `ttl_ms` and within `intercept.min_ttl_ms`–`intercept.max_ttl_ms`. This has nothing to do with how often URL lists are refreshed.
{{% /details %}}

## List File Format

Whether loaded from `url` or `file`, keen-pbr expects one entry per line:

- IPv4 address: `93.184.216.34`
- IPv4 {{< term "CIDR" >}}: `10.0.0.0/8`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- IPv6 CIDR: `2001:db8::/32`
- Domain: `example.com` — covers the domain and all its subdomains
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

Both entries cover the domain and all its subdomains. Writing `*.example.com` is equivalent to `example.com` — the `*.` prefix is stripped automatically by keen-pbr.

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

All four sources are merged into a single list. `ttl_ms: 86400000` means the IP addresses of this list's domains are kept for at least 24 hours. IP addresses from `ip_cidrs`, the file and the URL stay until the list itself changes.

