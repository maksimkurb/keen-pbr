---
title: Installation
weight: 1
---

## Install keen-pbr

Select the section for your platform below, then open the stable repository page and choose your OS in the selector on the left.

### Keenetic / NetCraze

keen-pbr can be installed on Keenetic / NetCraze routers via the Entware `opkg` package manager.

#### Install Entware on the router

First, ensure Entware is installed on your router. Please consult the official router manual [for Keenetic](https://support.keenetic.com/?lang=en) or [NetCraze](https://support.netcraze.ru/?lang=en) <!-- replace with ?lang=ru for Russian -->:
1. Find your router's manual
2. In the search field, search for "Installing the Entware repository on a USB drive" <!-- (or Установка репозитория Entware на USB-накопитель in Russian) -->
3. Read the instructions carefully

#### Install required components

Open your router configuration page, navigate to **Management** -> **System settings**, and install these additional components:

- Network Functions / **IPv6 Protocol** (required to install **Netfilter Subsystem Kernel Modules**)
- OPKG Packages / **Open Package System Support**
- OPKG Packages / **Netfilter Subsystem Kernel Modules**
- OPKG Packages / **Xtables-addons Extension Package for Netfilter**

#### Install from the repository page

Open the repository instructions page, select **Keenetic / NetCraze** in the OS selector on the left, and use the generated commands:

- https://repo.keen-pbr.fyi/repository/stable

Example install command:

```bash {filename="bash"}
opkg install keen-pbr
# or if you want headless version (without API and without WebUI)
# opkg install keen-pbr-headless
```

The package installs its config at `/opt/etc/keen-pbr/config.json` and enables the init script automatically.

Start the service:

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr start
```

Restart it after editing the config:

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr restart
```

### OpenWrt

Open the repository instructions page, select **OpenWrt** in the OS selector on the left, and use the generated commands for your exact version and architecture:

- https://repo.keen-pbr.fyi/repository/stable

The page automatically shows the correct flow for your target:

- OpenWrt 25.x and newer: `apk`
- OpenWrt 24.x and older: `opkg`

For OpenWrt targets that use `opkg`, the install command looks like this:

```bash {filename="bash"}
opkg install keen-pbr
# or if you want headless version (without API and without WebUI)
# opkg install keen-pbr-headless
```

After installation, the config file is at `/etc/keen-pbr/config.json`.

Start the service:

```bash {filename="bash"}
service keen-pbr start
```

Enable autostart:

```bash {filename="bash"}
service keen-pbr enable
```

### Debian

Open the repository instructions page, select **Debian** in the OS selector on the left, and use the generated commands for your release and architecture:

- https://repo.keen-pbr.fyi/repository/stable

The generated instructions include the signing key, the `apt` source entry, and the install command.

After installation, the config file is at `/etc/keen-pbr/config.json`.

Check the service status:

```bash {filename="bash"}
systemctl status keen-pbr
```

Restart it after editing the config:

```bash {filename="bash"}
systemctl restart keen-pbr
```

{{< callout type="info" >}}
  If you do not plan to use the keen-pbr Web UI or API, you can install the `keen-pbr-headless` package instead.
  It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

## Post-Install

After installation, the default config path is `/opt/etc/keen-pbr/config.json` on Keenetic / NetCraze and `/etc/keen-pbr/config.json` on OpenWrt and Debian. See the [Quick Start](../quick-start/) guide for a minimal working configuration, or the full [Configuration](../../configuration/) reference.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}

## CLI Flags

| Flag | Description |
|---|---|
| `--config <path>` | Path to the JSON config file (default depends on the build target, e.g. `/etc/keen-pbr/config.json` on OpenWrt and Debian, and `/opt/etc/keen-pbr/config.json` on Keenetic) |
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

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```
