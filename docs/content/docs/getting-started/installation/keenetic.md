---
title: Keenetic / NetCraze
weight: 1
---

keen-pbr can be installed on Keenetic / NetCraze routers via the Entware `opkg` package manager.

## 1. Install Entware on the router

First, ensure Entware is installed on your router. Please consult the official router manual [for Keenetic](https://support.keenetic.com/?lang=en) or [NetCraze](https://support.netcraze.ru/?lang=en):

1. Find your router's manual.
2. In the search field, search for "Installing the Entware repository on a USB drive".
3. Read and follow the instructions carefully.

## 2. Install required components

Open your router configuration page, navigate to **Management** -> **System settings**, and install these additional components:

- Network Functions / **IPv6 Protocol** (required to install **Netfilter Subsystem Kernel Modules**)
- OPKG Packages / **Open Package System Support**
- OPKG Packages / **Netfilter Subsystem Kernel Modules**
- OPKG Packages / **Xtables-addons Extension Package for Netfilter**

## 3. Install from the repository page

Open the repository instructions page, select **Keenetic / NetCraze** in the OS selector on the left, and use the generated commands:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable" >}}

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

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, you can install the `keen-pbr-headless` package instead.
It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

After installation, see the [Quick Start](../quick-start/) guide for a minimal working configuration, or the full [Configuration](../../configuration/) reference.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}
