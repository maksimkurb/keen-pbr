---
title: Installation
weight: 1
---

## Keenetic

keen-pbr can be installed on Keenetic routers via the Entware opkg package manager.

First, ensure Entware is installed on your router. Then add the keen-pbr feed and install the package:

```bash
opkg update
opkg install keen-pbr
```

The config file is placed at `/etc/keen-pbr/config.json`. Edit it before starting the service.

Start the daemon:

```bash
/opt/sbin/keen-pbr --config /etc/keen-pbr/config.json -d
```

To start automatically on boot, add the command to your router's startup scripts.

## OpenWRT

On OpenWRT, install via opkg:

```bash
opkg update
opkg install keen-pbr
```

After installation, the config file is at `/etc/keen-pbr/config.json`. Start the service:

```bash
service keen-pbr start
```

Enable autostart:

```bash
service keen-pbr enable
```

## Post-Install

After installation, the default config path is `/etc/keen-pbr/config.json`. See the [Quick Start](../quick-start/) guide for a minimal working configuration, or the full [Configuration](../../configuration/) reference.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}

## CLI Flags

| Flag | Description |
|---|---|
| `--config <path>` | Path to the JSON config file (default: `/etc/keen-pbr/config.json`) |
| `-d` | Run as daemon (daemonize) |
| `--no-api` | Disable the HTTP API even if configured |
| `--version` | Print version and exit |
| `--help` | Print help and exit |

## Signals

| Signal | Action |
|---|---|
| `SIGUSR1` | Re-verify routing tables and trigger immediate URL tests |
| `SIGHUP` | Full reload: re-download lists, re-apply firewall and routing rules |
| `SIGTERM` / `SIGINT` | Graceful shutdown |

Example full reload via signal:

```bash
kill -HUP $(cat /var/run/keen-pbr.pid)
```
