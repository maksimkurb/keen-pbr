# Debian

keen-pbr can be installed on Debian systems from the keen-pbr APT repository.

## 1. Add the repository from the repository page

Open the repository instructions page, select **Debian** in the OS selector on the left, and use the generated commands for your release and architecture:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable/?lang=en" >}}

The generated instructions include the signing key and the `apt` source entry.

## 2. Install the package

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

Next step: open [Quick Start](../quick-start/) and use the **Web UI** tab for the easiest first setup. If you installed `keen-pbr-headless`, use the **JSON / CLI** tab instead.

{{< callout type="info" >}}
If pre-built packages are not yet available for your platform, see [Build from Source](../compilation/) to compile keen-pbr yourself.
{{< /callout >}}

