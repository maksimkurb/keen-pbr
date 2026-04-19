# Upgrade from 2.x

Use this guide if you already run keen-pbr 2.x on Keenetic / NetCraze and want to migrate to the current package and config format.

## 1. Replace the repository

Open the repository instructions page, select **Keenetic / NetCraze** in the OS selector on the left, select your router architecture, and follow the generated commands to replace the old keen-pbr 2.x repository with the current one:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable" >}}

Use the commands exactly as shown for Keenetic / NetCraze on the repository page.

{{< callout type="info" >}}
Before changing packages, keep a backup copy of your current 2.x config so you can compare it with the converted result if needed.
{{< /callout >}}

## 2. Install keen-pbr

After replacing the repository, install the current package.

### Keenetic / NetCraze

```bash {filename="bash"}
opkg install keen-pbr

# or if you want headless version (without API and without WebUI)
# opkg install keen-pbr-headless
```

Config path:

```text
/opt/etc/keen-pbr/config.json
```

## 3. Upgrade the config to the new format

Open the [Config Converter](/converter/)

Then:

1. Copy the content of `/opt/etc/keen-pbr/keen-pbr.conf` into the converter input field on the left.
2. Review the generated config.
3. Replace the content of `/opt/etc/keen-pbr/config.json` with the converted output.

{{< callout type="info" >}}
Replace the config file with the converted output only after you have reviewed it.
{{< /callout >}}

## 4. Restart keen-pbr

Restart the service after saving the config.

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr restart
```

After restart, open [Quick Start](../quick-start/) if you want to verify the service in the Web UI or continue with JSON / CLI setup.

