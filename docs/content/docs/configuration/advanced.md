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
| `strict_enforcement` | boolean | `false` | Default strict routing enforcement for interface outbounds. When enabled, an unreachable default route is installed if the outbound gateway/interface cannot be confirmed reachable. Can be overridden per-outbound. |
| `strict_enforcement_action` | string | `"unreachable"` | Terminal action for strict enforcement: `unreachable` returns an immediate network error; `blackhole` silently drops packets until the application times out. |
| `max_file_size_bytes` | integer | `8388608` (8 MiB) | Maximum allowed size in bytes for downloaded remote list content |
| `firewall_verify_max_bytes` | integer | `262144` | Maximum stdout bytes captured per firewall verification command (`0` = unlimited) |

```json { filename="config.json" }
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr",
    "firewall_backend": "auto",
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

Controls the firewall mark range used to tag packets for policy routing.

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

The `mask` must be exactly two adjacent hex nibbles (e.g. `0x00FF0000`). Outbounds are assigned sequential marks starting from `start`, masked by `mask`.

{{< callout type="warning" >}}
If other software on your system uses the same fwmark range, adjust `start` and `mask` to avoid conflicts.
{{< /callout >}}

## iproute

Controls the routing table ID range used for outbound-specific tables.

| Field | Type | Default | Description |
|---|---|---|---|
| `table_start` | integer | `150` | First routing table ID to allocate for outbounds |
| `rule_priority_start` | integer or null | `table_start` | First RPDB rule priority to allocate; null inherits `table_start` |

```json { filename="config.json" }
{
  "iproute": {
    "table_start": 150,
    "rule_priority_start": null
  }
}
```

Outbounds are assigned sequential table IDs starting from `table_start`. Ensure these IDs don't conflict with existing routing tables on your system.

Keen-pbr removes only the exact routes it created. Unrelated routes in an allocated
table are preserved during reload and shutdown; an identical pre-existing route is
treated as already satisfied and is not adopted for cleanup.

## Routing boundaries and failover

Rules are evaluated in order. Two identical matching rules are **not** a failover
pair: the first rule marks the packet for its outbound and terminates the chain,
so the second rule is unreachable. If that first interface disappears and strict
enforcement is disabled, its table has no usable route and RPDB continues to the
main table; traffic may use the ordinary default gateway. With strict enforcement,
the configured terminal action blocks that lookup instead.

Use one `urltest` outbound with both interfaces as candidates for real failover.
Set `conntrack_on_switch` to `preserve` (the default) to keep established flows
on their existing path, or `delete` to remove only that URLTEST outbound's owned
conntrack entries after its replacement route is active.

`ipv6_enabled: false` means IPv6 is unmanaged by keen-pbr; it is not an IPv6
block. Keen-pbr also does not intercept arbitrary client DNS, DoT, or DoH. Clients
must use the router resolver, or DNS enforcement must be provided separately.

Marks identify active configuration ownership only while that configuration is
unchanged. After a crash, changing the fwmark mask offline prevents the daemon
from identifying the old namespace. Dynamic DNS-set TTL expiry affects new
connections; existing conntrack flows can continue using their established path.

`keen-pbr download` refreshes cache files and can require a restart before the
runtime consumes changed lists. Use `keen-pbr download --reload` to refresh and
apply the relevant runtime state immediately.

## Single-instance requirement

Only one keen-pbr daemon is supported in a network namespace. Changing `pid_file`
does not create an isolated instance: firewall tables, chains, and sets use fixed
application-wide names. A second instance can replace or remove the first
instance's firewall state during apply, reload, or shutdown.

Use one daemon with multiple outbounds and rules. If separate instances are
required, place them in separate network namespaces with independent firewall
and routing state.

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

You can also trigger a manual refresh at any time:
- Send `SIGHUP` to the daemon process: `kill -HUP $(cat /var/run/keen-pbr.pid)`
