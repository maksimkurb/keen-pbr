---
title: Debian
weight: 3
---

keen-pbr is installed on Debian from the keen-pbr APT repository. Run the commands over SSH or in the server terminal as `root` (or with `sudo`).

{{% steps %}}

### Add the repository

Open the [keen-pbr repository page](https://repo.keen-pbr.fyi/repository/stable/?lang=en), select the OS **Debian** on the left, then your release and architecture. Run the commands it shows, in order: they add the signing key and the `apt` package source.

### Install the package

```bash {filename="bash"}
apt update
apt install keen-pbr

# version without the web UI and API:
# apt update
# apt install keen-pbr-headless
```

The configuration is in `/etc/keen-pbr/config.json`. The service starts right away and starts at every system boot.

{{< callout type="info" >}}
`keen-pbr-headless` takes less space: about 1.2 MB instead of 2.8 MB. It has no web UI and no API, so you configure it through the config file. If in doubt, install the regular `keen-pbr`.
{{< /callout >}}

### Check that the service is running

```bash {filename="bash"}
systemctl status keen-pbr
```

If everything is fine, the output contains `active (running)`.

### Open the web UI

Open `http://<server-ip>:12121` in your browser. The headless version has no web UI.

### Set up your first rules

Go to the [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) page and open the **Web UI** tab. If you installed `keen-pbr-headless`, open the **JSON** tab.

{{< callout type="info" >}}
If there are no ready-made packages for your platform, build keen-pbr yourself: [Build from Source]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Service commands

```bash {filename="bash"}
systemctl start keen-pbr
systemctl status keen-pbr
systemctl restart keen-pbr
```

## Separate DNS servers for lists (dnsmasq) {#dnsmasq}

dnsmasq is not needed for routing. It is a DNS server installed on the system next to keen-pbr. Install it if you want to send the domains of some lists to separate DNS servers, or set your own default DNS servers. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Install dnsmasq {#dnsmasq-install}

```bash {filename="bash"}
apt install dnsmasq
```

To apply the recommended dnsmasq settings, run:

```bash {filename="bash"}
/usr/lib/keen-pbr/dnsmasq-hook.sh install-recommended-config
```

It replaces `/etc/dnsmasq.conf` and saves the previous file as `/etc/dnsmasq.conf.backup-keenpbr`. dnsmasq then answers only your local networks and takes its servers from keen-pbr.

Then, in the Web UI, open **General** > **Settings** > **Local DNS server management** and turn on **Manage dnsmasq configuration**. Point the system resolver (or your DHCP server) at dnsmasq if it is not already used.

### Return to how it was {#dnsmasq-rollback}

1. Turn off **Manage dnsmasq configuration** and apply the config. dnsmasq restarts and uses the servers from `/etc/keen-pbr/dnsmasq-fallback.conf`. If dnsmasq has servers of its own, these are added to them; clear the file to avoid that.
2. Restore the system resolver you used before dnsmasq.
3. If you ran `install-recommended-config`, put the old settings back:
   ```bash {filename="bash"}
   cp /etc/dnsmasq.conf.backup-keenpbr /etc/dnsmasq.conf
   systemctl restart dnsmasq
   ```
4. Or remove the package:
   ```bash {filename="bash"}
   apt remove dnsmasq
   ```
