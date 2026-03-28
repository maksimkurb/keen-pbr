---
title: Advanced
weight: 5
---

This page covers the remaining top-level configuration keys: `daemon`, `api`, `fwmark`, `iproute`, and `lists_autoupdate`.

## daemon

Controls the PID file path and cache directory.

| Field | Type | Default | Description |
|---|---|---|---|
| `pid_file` | string | — | Path to write the PID file |
| `cache_dir` | string | `/var/cache/keen-pbr` | Directory for cached list data |

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr"
  }
}
```

The cache directory stores downloaded remote lists so they are available if the network is unreachable at startup.

## api

Controls the embedded HTTP API server.

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enable the HTTP API |
| `listen` | string | `"127.0.0.1:8080"` | Address and port to listen on |

```json
{
  "api": {
    "enabled": true,
    "listen": "127.0.0.1:8080"
  }
}
```

The API can also be disabled at runtime with `--no-api`, regardless of the config setting. See [API](../api/) for endpoint documentation.

## fwmark

Controls the firewall mark range used to tag packets for policy routing.

| Field | Type | Default | Description |
|---|---|---|---|
| `start` | string | `"0x00010000"` | First fwmark value to assign to outbounds |
| `mask` | string | `"0x00FF0000"` | Fwmark bitmask |

```json
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

```json
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

```json
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
- Call `POST /api/reload`
