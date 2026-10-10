---
title: Advanced
weight: 5
---

Most users can skip this page.

These settings are for advanced setups and service-level tuning: `daemon`, `api`, `fwmark`, `iproute`, and `lists_autoupdate`.

## daemon

Controls the PID file path, cache directory, and global routing behaviour.

| Field | Type | Default | Description |
|---|---|---|---|
| `pid_file` | string | — | Path to write the PID file |
| `cache_dir` | string | `/var/cache/keen-pbr` | Directory for cached list data |
| `firewall_backend` | string | `"auto"` | Firewall backend selection: `auto`, `iptables`, or `nftables` |
| `clear_dynamic_sets_on_apply` | boolean | `false` | Clear the dynamic sets (domain IP addresses collected from DNS replies and connections) on every config apply or service restart. List refreshes and link switches never clear them. |
| `ipset_hashsize` | integer | absent | Optional initial hash table size for ipsets created by the iptables backend (default `1024`). Has no effect with nftables. Must be between 1 and 2147483648. Changing it recreates the sets and empties the dynamic sets. |
| `ipset_maxelem` | integer | absent | Optional maximum element count for ipsets created by the iptables backend (default `65536`). Has no effect with nftables. Must be between 1 and 4294967295. Changing it recreates the sets and empties the dynamic sets. |
| `reuse_static_sets_on_runtime_refresh` | boolean | `true` | Do not refill list sets on `SIGUSR1`, `urltest`/`icmptest` switches and interface state changes: only rules are updated, which is faster. If `false`, sets are refilled every time. |
| `strict_enforcement` | boolean | `false` | If the interface or gateway of an `interface` outbound is down, do not let its traffic go out through the normal connection: the outbound's table gets the route from `strict_enforcement_action` instead. Can be set per outbound. |
| `strict_enforcement_action` | string | `"unreachable"` | What to do with traffic under `strict_enforcement`: `unreachable` returns an immediate network error; `blackhole` silently drops packets until the application times out. |
| `max_file_size_bytes` | integer | `8388608` (8 MiB) | Maximum allowed size in bytes for downloaded remote list content |
| `firewall_verify_max_bytes` | integer | `262144` | How many bytes of `iptables`/`nft` output to read when checking firewall rules (`keen-pbr status`, status page); `0` means no limit |

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
    "strict_enforcement": false,
    "strict_enforcement_action": "unreachable",
    "max_file_size_bytes": 8388608,
    "firewall_verify_max_bytes": 262144
  }
}
```

The cache directory stores downloaded remote lists so they are available if the network is unreachable at startup.

## api

Controls the embedded HTTP API server.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable the HTTP API |
| `listen` | string | `"0.0.0.0:12121"` | Address and port to listen on |

```json { filename="config.json" }
{
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121"
  }
}
```

The API can also be disabled at runtime with `--no-api`, regardless of the config setting. See [REST API]({{< relref "/docs/rest-api" >}}) for endpoint documentation and runtime controls.

## fwmark

Sets the range of {{< term "fwmark" >}} values keen-pbr puts on packets to send them to the right routing table. See [fwmark and conntrack]({{< relref "/docs/concepts#fwmark-and-conntrack" >}}).

| Field | Type | Default | Description |
|---|---|---|---|
| `start` | string | `"0x00010000"` | First fwmark value to assign to outbounds |
| `mask` | string | `"0x00FF0000"` | Fwmark bitmask |

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

Sets the routing table numbers and `ip rule` priorities keen-pbr uses for outbounds.

| Field | Type | Default | Description |
|---|---|---|---|
| `table_start` | integer | `150` | First routing table ID to allocate for outbounds |
| `rule_priority_start` | integer or null | `table_start` | Priority of the first `ip rule` entry; `null` means the same as `table_start` |
| `process_router_traffic` | boolean | `false` | Apply route rules to traffic of the router itself and collect domain IPs from it. When `false`, keen-pbr handles only traffic of devices in the network. `detour` of DNS servers works either way. |

```json { filename="config.json" }
{
  "iproute": {
    "table_start": 150,
    "rule_priority_start": null,
    "process_router_traffic": false
  }
}
```

Outbounds are assigned sequential table IDs starting from `table_start`. Ensure these IDs don't conflict with existing routing tables on your system.

keen-pbr removes only the routes it added itself. Routes added by you or by other software stay in place when keen-pbr reloads or stops.

## Failover

Rules are checked top to bottom and the first match wins. Two identical rules with different outbounds therefore do not give you a backup: packets never reach the second rule.

If the first outbound's interface goes down, the result depends on `strict_enforcement`:

- off (the default): traffic falls back to the ISP's default gateway, bypassing the VPN;
- on: traffic is blocked (`strict_enforcement_action` decides how).

To switch traffic to a backup link, create one `urltest` outbound and put both interfaces in it. `conntrack_on_switch` controls what happens to open connections when `urltest` moves to a faster link: `preserve` (the default) keeps them on the old link, `delete` deletes their {{< term "conntrack" >}} entries, so applications reconnect through the new one. When the selected link stops working, conntrack entries of its connections are always deleted.

## What keen-pbr does not do

- `ipv6_enabled: false` does not block IPv6. keen-pbr simply stops routing IPv6 traffic, and it flows as usual.
- keen-pbr learns domain IPs from ordinary DNS replies passing through the router, even when a client uses a third-party DNS server. It cannot read encrypted DNS ({{< term "DoH" >}}, {{< term "DoT" >}}). If a device uses it, keen-pbr learns the site's address only from the connection itself (the server name in TLS, HTTP or {{< term "QUIC" >}}), and the first connections may bypass the VPN. To avoid this, turn off "secure DNS" on the device and set up DoH/DoT on the router itself.
- When a learned domain IP expires from a list, only new connections are affected. Connections that are already open keep their route, because their {{< term "fwmark" >}} is stored in {{< term "conntrack" >}}.
- `keen-pbr download` only downloads lists into the cache. To make the running service use them, run `keen-pbr download --reload` or restart the service.
- Do not change `fwmark.mask` while keen-pbr is stopped after a crash. On the next start it will not recognise its old rules and cannot remove them. If that has already happened, reboot the router.

## One instance only

Run only one keen-pbr per router. A separate `pid_file` does not help: a second instance uses the same firewall rule and set names and breaks the first one's rules.

Put all outbounds and routing rules into one configuration. If you really need two independent instances, run them in separate network namespaces.

## lists_autoupdate

Controls automatic periodic refresh of remote lists.

If the `lists_autoupdate` section is omitted, automatic refresh is disabled.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable automatic list refresh |
| `cron` | string | — | Standard 5-field cron expression for the refresh schedule |

```json { filename="config.json" }
{
  "lists_autoupdate": {
    "enabled": true,
    "cron": "0 4 * * 0"
  }
}
```

The `cron` field uses the standard 5-field format: `minute hour day-of-month month day-of-week`. The example above runs weekly at 04:00 on Sunday.

The `cron` field is validated even when `enabled` is `false`.

If a remote list could not be downloaded at startup, the daemon keeps retrying it in the background with increasing delays (10 seconds, 30 seconds, 2 minutes, then every 5 minutes) until it succeeds. This does not depend on `lists_autoupdate`. The first download of a list that is not cached yet happens before keen-pbr applies the new dnsmasq settings, so per-list DNS servers may not be used for it. Later retries use them.

You can also trigger a manual refresh at any time:
- Send `SIGHUP` to the daemon process: `kill -HUP $(cat /var/run/keen-pbr.pid)`
