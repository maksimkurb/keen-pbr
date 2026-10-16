---
title: Upgrade from 2.x
---

This guide is for those who already run keen-pbr 2.x on Keenetic / NetCraze. You will replace the repository and the package and move your configuration to the new format. Run all commands over SSH.

## 1. Make a backup

Save your current 2.x configuration. If something goes wrong, you can compare it with the converted result:

```bash {filename="bash"}
cp /opt/etc/keen-pbr/keen-pbr.conf /opt/etc/keen-pbr/keen-pbr.conf.bak
```

## 2. Replace the repository

Open the repository page, select the OS **Keenetic / NetCraze** on the left and your router architecture. Run the commands it shows, in order, to replace the old keen-pbr 2.x repository with the current one:

{{< hextra/hero-button text="keen-pbr repository" link="https://repo.keen-pbr.fyi/repository/stable/?lang=en" >}}

## 3. Install keen-pbr

```bash {filename="bash"}
opkg install keen-pbr

# version without the web UI and API:
# opkg install keen-pbr-headless
```

The new configuration is in `/opt/etc/keen-pbr/config.json`.

## 4. Convert the config to the new format

Open the [Config Converter](https://keen-pbr.fyi/converter/). Then:

1. Copy the content of `/opt/etc/keen-pbr/keen-pbr.conf` into the converter input field on the left.
2. Review the generated config.
3. Replace the content of `/opt/etc/keen-pbr/config.json` with the converted output.

{{< callout type="info" >}}
Replace the config file only after you have reviewed the converted output.
{{< /callout >}}

## 5. Restart keen-pbr

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr restart
/opt/etc/init.d/S80keen-pbr status
```

If everything is fine, `status` prints `alive`. If it says `dead`, open [Service does not start]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

Then open [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}): the **Web UI** tab helps you check the setup, and the **JSON** tab is for working with the config file.
