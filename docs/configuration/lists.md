# Lists

A list is a set of sites or IP ranges. keen-pbr sends traffic to them according to your [route rules]({{< relref "/docs/configuration/route-rules" >}}).

**In the Web UI:** open **Routing** > **Lists** and click **Add list**. Enter a name and pick a source: **URL**, **File on device** or **Domains / IPs**. Then click **Apply** in the draft bar.

In the config file, the `lists` key is an object where each key is the list name and the value is its settings. A list name uses lowercase Latin letters, digits and `_`, starts with a letter, and is at most 24 characters long (`^[a-z][a-z0-9_]*$`).

Most of the time a simple domain list is enough:

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
| `domains` | array of string | no | Domains written directly in the config |
| `ip_cidrs` | array of string | no | IP addresses and subnets ({{< term "CIDR" >}}) written directly in the config |
| `file` | string | no | Path to a local list file |
| `ttl_ms` | integer | no (default: `0`) | Minimum time to keep the IP addresses of this list's domains, in milliseconds. In the Web UI: **IP cache duration (ms)**. If unset or below 1000, `intercept.min_ttl_ms` is used. If the {{< term "TTL" >}} in the DNS reply is longer, the address is kept longer, but no more than `intercept.max_ttl_ms` |
| `detour` | string | no | Tag of the outbound used to download the list from `url`. In the Web UI: **Make requests via Outbound**. Useful when the list source is not reachable directly |

Domains are written the same way in every source. `google.com` covers the domain and all its subdomains; `*.google.com` means the same, so you do not need the `*.` prefix. Invalid domains are skipped in files and URL lists, and cause a configuration error in `domains`.

URL lists are downloaded only over HTTP or HTTPS, including from addresses on your local network.

New URL lists are downloaded in the background right after the configuration is applied, already through the chosen `detour`. Until a list is downloaded, its rule affects nothing. If the download fails, the warnings show "List 'name' is not downloaded yet" with the error text, and keen-pbr keeps retrying. As soon as a urltest or icmptest outbound picks a working link, the retry runs immediately.

A local file must be a regular file (not a symlink). File size is limited by `daemon.max_file_size_bytes` (8 MiB by default), and line length by 4096 bytes.

A list must have at least one of `url`, `domains`, `ip_cidrs` or `file`.

{{< callout type="warning" >}}
If a list has several sources, their entries are merged. This is not recommended; create separate lists instead.
{{< /callout >}}

## List File Format

A file from `url` or `file` has one entry per line:

- IPv4 address: `93.184.216.34`
- IPv4 {{< term "CIDR" >}}: `10.0.0.0/8`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- IPv6 CIDR: `2001:db8::/32`
- Domain: `example.com` — covers the domain and all its subdomains (`*.example.com` is not needed; it is read the same way)
- Comments: lines starting with `#` are ignored
- Empty lines are ignored

## Examples

### Remote URL list {#remote-url-list}

One domain is often not enough: YouTube, for example, serves video from `googlevideo.com` and images from `ytimg.com`. A ready-made list is easier. The [v2fly/domain-list-community](https://github.com/v2fly/domain-list-community) project keeps such lists; the one for YouTube is [data/youtube](https://github.com/v2fly/domain-list-community/blob/master/data/youtube). In keen-pbr, use the link to the raw file (the **Raw** button on GitHub), as in the example below.

In the web UI: **Routing** > **Lists** > **Add list**, source **URL**.

Some lines in v2fly lists carry extra markers (`full:…`, `… @cn`). keen-pbr skips such lines and logs a warning about them; the other domains work.

```json { filename="config.json" }
{
  "lists": {
    "youtube": {
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/youtube"
    }
  }
}
```

Downloaded files are cached in `daemon.cache_dir`. If the URL is unreachable at startup, the cached copy is used.

{{< callout type="warning" >}}
So that an unchanged list is not downloaded again on every update, the remote server must support `If-Modified-Since` or `ETag`.
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

Each entry covers the domain and all its subdomains.

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

### IP address lifetime for domains

To keep the IP addresses of a list's domains for at least a day, set `ttl_ms`:

```json { filename="config.json" }
{
  "lists": {
    "my_domains": {
      "domains": ["example.com"],
      "ttl_ms": 86400000
    }
  }
}
```

