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
| `max_file_size_bytes` | integer | `8388608` (8 MiB) | Maximum allowed size in bytes for downloaded remote list content |
| `firewall_verify_max_bytes` | integer | `262144` | Maximum stdout bytes captured per firewall verification command (`0` = unlimited) |

```json { filename="config.json" }
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr",
    "firewall_backend": "auto",
    "strict_enforcement": false,
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

The API can also be disabled at runtime with `--no-api`, regardless of the config setting. See [REST API](../../rest-api/) for endpoint documentation and runtime controls.

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

```json { filename="config.json" }
{
  "iproute": {
    "table_start": 150
  }
}
```

Outbounds are assigned sequential table IDs starting from `table_start`. Ensure these IDs don't conflict with existing routing tables on your system.

## lists_autoupdate

Controls automatic periodic refresh of remote lists.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable automatic list refresh |
| `cron` | string | — | Standard 5-field cron expression for the refresh schedule |

```json { filename="config.json" }
{
  "lists_autoupdate": {
    "enabled": true,
    "cron": "0 4 * * *"
  }
}
```

The `cron` field uses the standard 5-field format: `minute hour day-of-month month day-of-week`. The example above runs at 04:00 every day.

The `cron` field is validated even when `enabled` is `false`.

You can also trigger a manual refresh at any time:
- Send `SIGHUP` to the daemon process: `kill -HUP $(cat /var/run/keen-pbr.pid)`
