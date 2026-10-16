---
title: OpenWrt
weight: 2
---

keen-pbr is installed on OpenWrt from the keen-pbr package repository. You run the commands over SSH.

{{% steps %}}

### Find out which package manager your OpenWrt uses

A package manager is the program that installs and updates other programs. Which one you have depends on the version:

- OpenWrt 25.x and newer: `apk`
- OpenWrt 24.x and older: `opkg`

### Install keen-pbr

1. Connect to the router over SSH.
2. Open the [keen-pbr repository page](https://repo.keen-pbr.fyi/repository/stable/?lang=en), select the OS **OpenWrt** on the left, then your version and architecture. Run the commands it shows, in order.
3. Install the package:

```bash {filename="bash"}
# OpenWrt 25.x and newer
apk update
apk add keen-pbr

# version without the web UI and API:
# apk add keen-pbr-headless
```

```bash {filename="bash"}
# OpenWrt 24.x and older
opkg update
opkg install keen-pbr

# version without the web UI and API:
# opkg install keen-pbr-headless
```

The configuration is in `/etc/keen-pbr/config.json`. The service starts by itself every time the router boots.

{{< callout type="info" >}}
`keen-pbr-headless` takes less space: about 1.2 MB instead of 2.8 MB. It has no web UI and no API, so you configure it through the config file. If in doubt, install the regular `keen-pbr`.
{{< /callout >}}

{{< callout type="warning" >}}
The package installs the kernel modules it needs. If the log contains `cannot bind netfilter queue/log ... Invalid argument`, install them manually: `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` and `kmod-nft-queue`.
{{< /callout >}}

### Check that the service is running

```bash {filename="bash"}
service keen-pbr status
```

If everything is fine, the command prints `running`.

### Open the web UI

Open `http://<router-ip>:12121` in your browser, for example `http://192.168.1.1:12121`. The headless version has no web UI.

### Set up your first rules

Go to the [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) page and open the **Web UI** tab. If you installed `keen-pbr-headless`, open the **JSON** tab.

{{< callout type="info" >}}
If there are no ready-made packages for your platform, build keen-pbr yourself: [Build from Source]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Service commands

```bash {filename="bash"}
service keen-pbr start
service keen-pbr restart
service keen-pbr status
service keen-pbr enable
```

`enable` turns on autostart when the router boots.

## Separate DNS servers for lists (dnsmasq) {#dnsmasq}

On OpenWrt, dnsmasq already works as the router's DNS server. keen-pbr does not need it for routing. To decide whether to let keen-pbr manage it, see [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Turn it on {#dnsmasq-install}

In the Web UI, open **General** > **Settings** > **Local DNS server management** and turn on **Manage dnsmasq configuration**.

While management is on and **Default upstream DNS servers** are set, keen-pbr takes the servers without a domain out of the list in `/etc/config/dhcp` (LuCI **DNS forwardings**). Per-domain servers such as `/lan/192.168.1.1` stay. The removed servers return when you turn management off or clear those servers.

### Return to how it was {#dnsmasq-rollback}

Turn off **Manage dnsmasq configuration** and apply the config. keen-pbr restarts dnsmasq and returns the removed servers.
