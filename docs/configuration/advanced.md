# Advanced

Most users can skip this page.

These settings are for advanced setups and service-level tuning: `daemon`, `api`, `fwmark`, `iproute`, and `lists_autoupdate`.

Some of these settings are in the web UI: **General** > **Settings** > **General**, **Lists autoupdate** and **Advanced routing settings**. The low-level fields appear after you switch **Settings mode** to **Advanced**. This page describes every `config.json` field; next to a field you will find where it is in the UI.

## daemon

Controls the PID file path (the file holding the service's process number), the cache directory, and general routing behaviour.

| Field | Type | Default | Description |
|---|---|---|---|
| `pid_file` | string | — | Path to write the PID file. Can be overridden with the `--pid-file` flag |
| `cache_dir` | string | `/var/cache/keen-pbr` | Directory for cached list data |
| `firewall_backend` | string | `"auto"` | Which firewall to use: `auto` (pick automatically), `iptables`, or `nftables` (not on Keenetic) |
| `clear_dynamic_sets_on_apply` | boolean | `false` | Clear the dynamic sets (domain IP addresses collected from DNS replies and connections) on every config apply or service restart. List refreshes and link switches never clear them. Web UI: **Clear learned domain addresses on apply**. |
| `ipset_hashsize` | integer | absent | {{< only iptables >}} Optional initial hash table size for ipsets created by the iptables backend (default `1024`). Has no effect with nftables. Must be between 1 and 2147483648. Web UI (**Advanced** mode): **IPSet hash table size (ipset hashsize)**. Changing it recreates the sets and empties the dynamic sets. |
| `ipset_maxelem` | integer | absent | {{< only iptables >}} Optional maximum element count for ipsets created by the iptables backend (default `65536`). Has no effect with nftables. Must be between 1 and 4294967295. Web UI: **Maximum entries in ipset (ipset maxelem)**. Changing it recreates the sets and empties the dynamic sets. |
| `reuse_static_sets_on_runtime_refresh` | boolean | `true` | Do not refill list sets on `SIGUSR1`, `urltest`/`icmptest` switches and interface state changes: only rules are updated, which is faster. If `false`, sets are refilled every time. |
| `skip_marked_packets` | boolean | `true` | Do not process packets that another program has already marked. Web UI: **Skip packets that are already marked**. |
| `ipv6_enabled` | boolean | `true` | Route IPv6 traffic. When `false`, IPv6 flows as usual, bypassing keen-pbr. Web UI: **Enable IPv6 support**. |
| `strict_enforcement` | boolean | `false` | If the interface or gateway of an `interface` outbound is down, do not let its traffic go out through the normal connection: the outbound's table gets the route from `strict_enforcement_action` instead. Can be set per outbound. Web UI: **Block traffic when an outbound is unavailable (kill-switch)**. |
| `strict_enforcement_action` | string | `"unreachable"` | What to do with traffic under `strict_enforcement`: `unreachable` returns an immediate network error; `blackhole` silently drops packets until the application times out. |
| `max_file_size_bytes` | integer | `8388608` (8 MiB) | Maximum allowed size in bytes for downloaded remote list content |
| `firewall_verify_max_bytes` | integer | `262144` | How many bytes of `iptables`/`nft` output to read when checking firewall rules (`keen-pbr status`, status page); `0` means no limit |
| `exec_timeout_seconds` | integer | `30` | How many seconds to wait for keen-pbr's helper scripts (for example the dnsmasq script) before stopping them. Minimum 1. |
| `exec_kill_grace_seconds` | integer | `2` | How many seconds to give a script to exit after it is stopped before it is killed. Minimum 0. |

```json { filename="config.json" }
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr",
    "firewall_backend": "auto",
    "clear_dynamic_sets_on_apply": false,
    "ipset_hashsize": null,
    "ipset_maxelem": null,
    "reuse_static_sets_on_runtime_refresh": true,
    "skip_marked_packets": true,
    "ipv6_enabled": true,
    "strict_enforcement": false,
    "strict_enforcement_action": "unreachable",
    "max_file_size_bytes": 8388608,
    "firewall_verify_max_bytes": 262144,
    "exec_timeout_seconds": 30,
    "exec_kill_grace_seconds": 2
  }
}
```

The cache directory stores downloaded remote lists so they are available if the network is unreachable at startup.

## api

Controls the embedded HTTP API server that the web UI runs on.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable the HTTP API and the web UI. The config installed by the `keen-pbr` package already has it on |
| `listen` | string | `"0.0.0.0:12121"` | Address and port to listen on |
| `max_request_body_bytes` | integer | `1048576` | Maximum request body size in bytes. Minimum 1024. |
| `read_timeout_seconds` | integer | `15` | How many seconds to wait for a request from the client. Minimum 1. |
| `write_timeout_seconds` | integer | `15` | How many seconds to send a response to the client. Minimum 1. |
| `keep_alive_timeout_seconds` | integer | `20` | How many seconds to keep a connection open between requests. Minimum 1. |
| `authentication` | object | off | Password login: `enabled` and `password_hash`. With `enabled: true` the hash is required. Create it with `keen-pbr hash-password --update` or in the web UI: **Security**. |
| `cors.allowed_origins` | array | — | Site addresses (origins) allowed to call the API from a browser, for example `"https://router.example"`. Exact `http://` or `https://` addresses only, no path or wildcards. |

```json { filename="config.json" }
{
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121",
    "max_request_body_bytes": 1048576,
    "read_timeout_seconds": 15,
    "write_timeout_seconds": 15,
    "keep_alive_timeout_seconds": 20,
    "authentication": { "enabled": false },
    "cors": { "allowed_origins": [] }
  }
}
```

Start `keen-pbr` with `--no-api` to disable the API regardless of the config. See [REST API]({{< relref "/docs/rest-api" >}}) for endpoint documentation and runtime controls.

## fwmark

Sets the range of {{< term "fwmark" >}} values. A fwmark is a label keen-pbr puts on a packet so the system sends it to the right routing table (the table holding one outbound's routes). See [fwmark and conntrack]({{< relref "/docs/concepts#fwmark-and-conntrack" >}}).

| Field | Type | Default | Description |
|---|---|---|---|
| `start` | string | `"0x00010000"` | First fwmark value to assign to outbounds. Web UI: **fwmark starting value** |
| `mask` | string | `"0x00FF0000"` | Fwmark bitmask. Web UI: **fwmark mask** |

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x00010000",
    "mask": "0x00FF0000"
  }
}
```

`mask` must be a run of hex `F` digits, for example `0x00FF0000`. Outbounds get sequential fwmark values starting from `start`, within `mask`.

{{< callout type="warning" >}}
If other software on your system uses the same fwmark range, adjust `start` and `mask` to avoid conflicts.
{{< /callout >}}

## iproute

Sets the routing table numbers and the priorities of the `ip rule` entries that keen-pbr creates for outbounds. An `ip rule` entry is a rule that picks a routing table by fwmark.

| Field | Type | Default | Description |
|---|---|---|---|
| `table_start` | integer | `150` | First routing table ID to allocate for outbounds. Web UI: **IP routing table starting value** |
| `rule_priority_start` | integer or null | `table_start` | Priority of the first `ip rule` entry; `null` means the same as `table_start` |
| `process_router_traffic` | boolean | `false` | Apply route rules to traffic of the router itself and collect domain IPs from it. When `false`, keen-pbr handles only traffic of devices in the network. `detour` of DNS servers works either way. Web UI (**Advanced** mode): **Process router's own traffic**. |

```json { filename="config.json" }
{
  "iproute": {
    "table_start": 150,
    "rule_priority_start": null,
    "process_router_traffic": false
  }
}
```

Outbounds are assigned sequential table IDs starting from `table_start`. Check that these IDs do not clash with existing routing tables. Do not start from reserved IDs: 0, 128, 250-260 and everything from 32000. A config with such a `table_start` fails validation, and reserved IDs are skipped when numbers are allocated.

When keen-pbr stops, it removes only its own routes. Routes added by you or by other software stay in place.

## Failover

Rules are checked top to bottom and the first match wins. Two identical rules with different outbounds therefore do not give you a backup: packets never reach the second rule.

If the first outbound's interface goes down, the result depends on `strict_enforcement`:

- off (the default): traffic falls back to the ISP's default gateway, bypassing the VPN;
- on: traffic is blocked (`strict_enforcement_action` decides how).

To switch traffic to a backup link, create one `urltest` (or `icmptest`) outbound and put both interfaces in it. `conntrack_on_switch` controls what happens to open connections when it moves to a faster link: `preserve` (the default) keeps them on the old link, `delete` deletes their {{< term "conntrack" >}} entries, so applications reconnect through the new one. When the selected link stops working, conntrack entries of its connections are always deleted.

## Good to know

- With `ipv6_enabled: false`, IPv6 traffic flows as usual, bypassing keen-pbr.
- keen-pbr learns domain IPs from ordinary DNS replies passing through the router, even when a client uses a third-party DNS server. If a device uses encrypted DNS ({{< term "DoH" >}}, {{< term "DoT" >}}), keen-pbr learns the site's address only from the connection itself (the server name in TLS, HTTP or {{< term "QUIC" >}}), and the first connections may bypass the VPN. To avoid this, turn off "secure DNS" on the device and set up DoH/DoT on the router itself.
- When a learned domain IP expires from a list, new connections take a different route, while connections that are already open keep their route, because their {{< term "fwmark" >}} is stored in {{< term "conntrack" >}}.
- `keen-pbr download` downloads lists into the cache. To make the running service use them, run `keen-pbr download --reload` or restart the service.
- If keen-pbr stopped after a crash, do not change `fwmark.mask` before the next start: otherwise it will not recognise its old rules and cannot remove them. If that has already happened, reboot the router.

## One instance only

Run only one keen-pbr per router. A separate `pid_file` does not help: a second instance uses the same firewall rule and set names and breaks the first one's rules.

Put all outbounds and routing rules into one configuration. If you need two independent instances, run them in separate network namespaces.

## lists_autoupdate

Scheduled refresh of remote lists.

If the `lists_autoupdate` section is omitted, automatic refresh is disabled. In the web UI: **General** > **Settings** > **Lists autoupdate**.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable automatic list refresh. Web UI: **Enable lists autoupdate** |
| `cron` | string | — | Refresh schedule: a 5-field cron expression (format below). Web UI: **Refresh schedule** |

```json { filename="config.json" }
{
  "lists_autoupdate": {
    "enabled": true,
    "cron": "0 4 * * 0"
  }
}
```

The `cron` format: `minute hour day-of-month month day-of-week`. The example above runs weekly at 04:00 on Sunday.

If `cron` is filled in, it is validated even when `enabled` is `false`. With `enabled: true` it is required.

If a remote list could not be downloaded at startup, the daemon keeps retrying it in the background with increasing delays (10 seconds, 30 seconds, 2 minutes, then every 5 minutes) until it succeeds. This does not depend on `lists_autoupdate`.

You can refresh the lists by hand at any time:

```bash
keen-pbr download --reload
```

