---
title: OpenWrt
weight: 2
---

Open the repository instructions page, select **OpenWrt** in the OS selector on the left, and use the generated commands for your exact version and architecture:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable" >}}

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

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, you can install the `keen-pbr-headless` package instead.
It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

After installation, see the [Quick Start](../quick-start/) guide for a minimal working configuration, or the full [Configuration](../../configuration/) reference.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}
