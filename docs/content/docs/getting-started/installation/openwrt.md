---
title: OpenWrt
weight: 2
---

keen-pbr can be installed on OpenWrt routers from the keen-pbr package repository.

{{% steps %}}

### Check which package manager your OpenWrt version uses

The repository page automatically shows the correct flow for your target:

- OpenWrt 25.x and newer: `apk`
- OpenWrt 24.x and older: `opkg`

### Install from the repository page

Open the repository instructions page, select **OpenWrt** in the OS selector on the left, and use the generated commands for your exact version and architecture: 

[{{< icon "server" >}} keen-pbr repository](https://repo.keen-pbr.fyi/repository/stable/?lang=en)

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

{{< callout type="warning" >}}
DNS and L7 interception needs the kernel modules `nfnetlink_queue`, `nfnetlink_log` and `nft_queue`.
The package pulls in `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` and `kmod-nft-queue` automatically. If you installed an older build or removed them, install them manually; otherwise the daemon logs `cannot bind netfilter queue/log ... Invalid argument` and `/api/health/service` names the missing module. `nft_log` is part of `kmod-nft-core`.
{{< /callout >}}

### Next steps

Open [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) and use the **Web UI** tab for the easiest first setup. If you installed `keen-pbr-headless`, use the **JSON / CLI** tab instead.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source]({{< relref "/docs/developer/build-from-source" >}}) to compile keen-pbr yourself.
{{< /callout >}}

{{% /steps %}}

## Upgrading from the dnsmasq integration

Older keen-pbr versions managed dnsmasq: they moved its upstream servers to `dhcp.@dnsmasq[*].kpbr_server`, added `conf-script` and jail mounts. This integration was removed, and `dnsmasq-full` is no longer required. On package upgrade keen-pbr undoes those changes automatically:

- the upstream servers saved in `kpbr_server` are restored into `server` (no duplicates, original order), then `kpbr_server` is deleted;
- the keen-pbr `addnmount` entries (`/usr/sbin/keen-pbr`, `/etc/keen-pbr`, `/var/cache/keen-pbr`, `/var/run/keen-pbr`) and the `keen-pbr.conf` drop-in in the dnsmasq `confdir` are removed;
- UCI `dhcp` is committed and dnsmasq is restarted once. A second run changes nothing, and dnsmasq sections that were never touched by keen-pbr are left alone.

You can re-run the migration manually with `/usr/lib/keen-pbr/uci.sh dnsmasq-migrate-from-keen-pbr`.
