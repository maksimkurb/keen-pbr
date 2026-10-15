---
title: Debian
weight: 3
---

keen-pbr can be installed on Debian systems from the keen-pbr APT repository.

{{% steps %}}

### Add the repository from the repository page

Open the repository instructions page, select **Debian** in the OS selector on the left, and use the generated commands for your release and architecture:

[keen-pbr repository](https://repo.keen-pbr.fyi/repository/stable/?lang=en)

The generated instructions include the signing key and the `apt` source entry.

### Install the package

Example install command:

```bash {filename="bash"}
apt update
apt install keen-pbr

# or if you want headless version (without API and without WebUI)
# apt update
# apt install keen-pbr-headless
```

The package installs its config at `/etc/keen-pbr/config.json` and enables the systemd service automatically.

Useful service commands:

```bash {filename="bash"}
systemctl start keen-pbr
systemctl status keen-pbr
systemctl restart keen-pbr
```

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, the `keen-pbr-headless` package uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

### Next steps

Open [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) and use the **Web UI** tab for the easiest first setup. If you installed `keen-pbr-headless`, use the **JSON / CLI** tab instead.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source]({{< relref "/docs/developer/build-from-source" >}}) to compile keen-pbr yourself.
{{< /callout >}}

{{% /steps %}}

## Separate DNS servers for lists (dnsmasq) {#dnsmasq}

keen-pbr does not need dnsmasq for routing. Install it only if you want separate DNS servers for some lists or your own default DNS servers. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Install dnsmasq {#dnsmasq-install}

```bash {filename="bash"}
apt install dnsmasq
```

To apply the recommended dnsmasq settings, run:

```bash {filename="bash"}
/usr/lib/keen-pbr/dnsmasq-hook.sh install-recommended-config
```

It replaces `/etc/dnsmasq.conf` and saves the previous file as `/etc/dnsmasq.conf.backup-keenpbr`. dnsmasq then answers only your local networks and takes its servers from keen-pbr.

Then, in the Web UI, open **General** > **Local DNS server management** and turn on **Manage dnsmasq configuration**. keen-pbr creates `/etc/dnsmasq.d/keen-pbr.conf`. Point the system resolver (or your DHCP server) at dnsmasq if it is not already used.

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
