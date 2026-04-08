---
title: Debian
weight: 3
---

Open the repository instructions page, select **Debian** in the OS selector on the left, and use the generated commands for your release and architecture:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable" >}}

The generated instructions include the signing key, the `apt` source entry, and the install command.

If you do not need the Web UI or API, install `keen-pbr-headless` instead of `keen-pbr`.

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
If you do not plan to use the keen-pbr Web UI or API, the `keen-pbr-headless` package uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. Also, you can disable API server via config flag at any time on the full package version.
{{< /callout >}}

After installation, see the [Quick Start](../quick-start/) guide for a minimal working configuration, or the full [Configuration](../../configuration/) reference.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}
