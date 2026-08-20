---
title: Keenetic / NetCraze
weight: 1
---

keen-pbr can be installed on Keenetic / NetCraze routers via Entware's `opkg` package manager.

{{% steps %}}

### Install Entware on the router

First, ensure that Entware is installed on your router. Please consult the official router manual for [Keenetic](https://support.keenetic.com/?lang=en) or [NetCraze](https://support.netcraze.ru/?lang=en):

1. Find your router's manual.
2. In the search field, search for "Installing the Entware repository on a USB drive".
3. Read and follow the instructions carefully.

### Install required components

Open your router's configuration page, navigate to **Management** -> **System settings**, and install the following additional components:

- Network Functions / **IPv6 Protocol** (On NDMS 5.0+ is absent because it is a part of core now)
- OPKG Packages / **Open Package System Support**
- OPKG Packages / **Netfilter Subsystem Kernel Modules**
- OPKG Packages / **Xtables-addons Extension Package for Netfilter**
- Utilities and services / **DNS-over-TLS proxy** (optional, but highly recommended)
- Utilities and services / **DNS-over-HTTPS proxy** (optional, but highly recommended)

### Configure DoH/DoT on the router

It is recommended to set up DoH/DoT DNS servers on your router to protect yourself from DNS spoofing attacks.

Consult the official Keenetic documentation on how to do it: [DoH and DoT proxy servers for DNS requests encryption](https://support.keenetic.com/titan/kn-1811/en/25049.html).

### Install `keen-pbr` from the repository


1. Open [keen-pbr repository page](https://repo.keen-pbr.fyi/repository/stable/?lang=en).
2. Select "**Keenetic / NetCraze**" in the OS selector on the left, choose the "**current**" version, and select the architecture that matches your router.
    - TIP: You can run `opkg print-architecture` in an SSH session to see your router's architecture.
3. Follow the instructions on the repository page carefully.
4. When you run the `opkg install keen-pbr` command, the installation script will prompt you to confirm whether you want to replace your `dnsmasq` file. If you are not sure, press <kbd>y</kbd> and then <kbd>Enter</kbd>.
    - Example install command:
      ```bash {filename="bash"}
      opkg install keen-pbr

      # or if you want headless version (without API and without WebUI)
      # opkg install keen-pbr-headless
      ```

### Ensure that `keen-pbr` service is up and running

Before continuing to the next step, it is recommended to check whether `keen-pbr` started successfully and has not crashed. Run `/opt/etc/init.d/S80keen-pbr status`. If it is dead, see [Troubleshooting]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

Example:
```
~ # /opt/etc/init.d/S80keen-pbr status
 Checking keen-pbr...              alive.
```

If the service is **alive**, continue to the next step.

### Enable `dns-override`

After installing `keen-pbr`, you have to enable `dns-override` so that all DNS requests from LAN clients are forwarded to Entware:

1. Open http://my.keenetic.net/a (if this link does not open, open `http://<router-ip>/a` instead)
2. Run the commands `opkg dns-override` and then `system configuration save`
3. Reboot the router. **_This is very important_: without a reboot, the option will not become active.**

### Configure the service

After you reboot the router, you can open http://my.keenetic.net:12121 to configure keen-pbr (if you installed the full version; the headless version does not provide a Web UI).

You can also configure keen-pbr manually by modifying the configuration file: `/opt/etc/keen-pbr/config.json`.

### Choose RAW or mangle PREROUTING

This choice is specific to Keenetic / NetCraze. Keenetic can periodically rebuild
its `mangle` table, for example after a DHCP lease renewal. The normal mangle
integration is then briefly absent until keen-pbr reapplies it. RAW is independent
of that rebuild, but runs before Keenetic's **Connection Policies** > **Policy
Bindings** rules. The platform's iptables replacement behaviour is also discussed in the
[Keenetic Community forum](https://forum.keenetic.ru/topic/20631-keenetic-ndms-%D0%BE%D1%87%D0%B8%D1%89%D0%B0%D0%B5%D1%82-iptables-%D0%B2%D0%BE-%D0%B2%D1%80%D0%B5%D0%BC%D1%8F-%D0%B7%D0%B0%D0%BF%D1%83%D1%81%D0%BA%D0%B0-%D0%BF%D1%80%D0%BE%D0%B3%D1%80%D0%B0%D0%BC%D0%BC%D1%8B/).

| Mode | Benefit | Trade-off |
|---|---|---|
| **RAW** (default when available) | PBR remains active across a Keenetic mangle rebuild. Long-running, sensitive flows such as gaming sessions avoid rebuild-related breaks, and a kill switch has fewer opportunities for packets to leak through the ordinary route. | After keen-pbr sets its `fwmark` in RAW, Keenetic's **Connection Policies** > **Policy Bindings** rules overwrite it. RAW mode therefore works only for devices assigned the **Default policy**. |
| **mangle** | keen-pbr runs after Keenetic's Connection Policy marks, so it can override routing for devices with a non-default policy. | When Keenetic rebuilds its routing table (usually about every five minutes), keen-pbr rules disappear briefly. A few packets can use the ordinary route before keen-pbr restores them, disrupting a gaming session or another sensitive flow. |

{{< callout type="info" >}}
For the most predictable Keenetic setup, do not assign **Connection Policies** to devices in Keenetic. Instead, create the equivalent per-device [keen-pbr route rules]({{< relref "/docs/configuration/route-rules" >}}) using the `src_addr` (**Source address**) field. This keeps routing policy in keen-pbr and avoids conflicts with Keenetic policy bindings.
{{< /callout >}}

The package default is `auto`: it probes IPv4 and IPv6 independently, using RAW
for each supported family and mangle for the other. Locally generated traffic
always remains in mangle OUTPUT. `enable`, `ipv4-only`, and `ipv6-only` are
available when a family must be forced; forced capability failures stop startup.

To see which mode was selected, restart the service and look for these messages
in the device system log:

Messages identify IPv4 and IPv6 capability resolution and the resulting daemon
flags (`--use-raw-prerouting` and `--use-raw6-prerouting`).

To always use mangle, edit `/opt/etc/keen-pbr/defaults`:

```sh
KEEN_PBR_RAW_PREROUTING="disable"
```

Then restart the service:

```sh
/opt/etc/init.d/S80keen-pbr restart
```

Set `KEEN_PBR_RAW_PREROUTING="enable"` to require RAW for every enabled family;
use `ipv4-only` or `ipv6-only` to force one family. The service fails to start
instead of falling back to mangle if a forced capability is unavailable.

Basic commands:

| Action | Command |
| --- | --- |
| Start service | `/opt/etc/init.d/S80keen-pbr start` |
| Restart service | `/opt/etc/init.d/S80keen-pbr restart` |
| Check if **keen-pbr** alive | `/opt/etc/init.d/S80keen-pbr status` |
| Check if **dnsmasq** alive | `/opt/etc/init.d/S56dnsmasq status` |

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, consider installing the `keen-pbr-headless` package.
It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. You can also disable the API server at any time via a config flag, even when using the full package version.
{{< /callout >}}

### Next steps

Go to the [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) page and use the **Web UI** tab for the easiest initial setup. If you installed the `headless` version, see the **JSON / CLI** tab instead.


{{% /steps %}}
