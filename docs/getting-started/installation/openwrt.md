# OpenWrt

keen-pbr can be installed on OpenWrt routers from the keen-pbr package repository.

## 1. Check which package manager your OpenWrt version uses

The repository page automatically shows the correct flow for your target:

- OpenWrt 25.x and newer: `apk`
- OpenWrt 24.x and older: `opkg`

## 2. Install from the repository page

Open the repository instructions page, select **OpenWrt** in the OS selector on the left, and use the generated commands for your exact version and architecture:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable" >}}

Example install commands:

```bash {filename="bash"}
# OpenWrt 25.x and newer
apk update
apk add keen-pbr

# or if you want headless version (without API and without WebUI)
# apk update
# apk add keen-pbr-headless
```

```bash {filename="bash"}
# OpenWrt 24.x and older
opkg update
opkg install keen-pbr

# or if you want headless version (without API and without WebUI)
# opkg update
# opkg install keen-pbr-headless
```

The package installs its config at `/etc/keen-pbr/config.json` and enables the init script automatically.

Useful service commands:

```bash {filename="bash"}
service keen-pbr start
service keen-pbr enable
service keen-pbr restart
```

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, you can install the `keen-pbr-headless` package instead.
It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

Next step: open [Quick Start](../quick-start/) and use the **Web UI** tab for the easiest first setup. If you installed `keen-pbr-headless`, use the **JSON / CLI** tab instead.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}

