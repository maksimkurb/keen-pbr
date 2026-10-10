---
title: Keenetic / NetCraze
weight: 1
---

keen-pbr can be installed on Keenetic / NetCraze routers via {{< term "Entware" >}}'s `opkg` package manager.

{{% steps %}}

### Install Entware on the router

First, ensure that {{< term "Entware" >}} is installed on your router. Please consult the official router manual for [Keenetic](https://support.keenetic.com/?lang=en) or [NetCraze](https://support.netcraze.ru/?lang=en):

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

It is recommended to set up {{< term "DoH" >}}/DoT DNS servers on your router to protect yourself from DNS spoofing attacks.

Consult the official Keenetic documentation on how to do it: [DoH and DoT proxy servers for DNS requests encryption](https://support.keenetic.com/titan/kn-1811/en/25049.html).

### Install `keen-pbr` from the repository


1. Open [keen-pbr repository page](https://repo.keen-pbr.fyi/repository/stable/?lang=en).
2. Select "**Keenetic / NetCraze**" in the OS selector on the left, choose the "**current**" version, and select the architecture that matches your router.
    - TIP: You can run `opkg print-architecture` in an SSH session to see your router's architecture.
3. Follow the instructions on the repository page carefully.
4. Run the `opkg install keen-pbr` command.
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

After installing `keen-pbr`, you have to enable `dns-override` so that all DNS requests from LAN clients are forwarded to {{< term "Entware" >}}:

1. Open http://my.keenetic.net/a (if this link does not open, open `http://<router-ip>/a` instead)
2. Run the commands `opkg dns-override` and then `system configuration save`
3. Reboot the router. **_This is very important_: without a reboot, the option will not become active.**

### Configure the service

After you reboot the router, you can open http://my.keenetic.net:12121 to configure keen-pbr (if you installed the full version; the headless version does not provide a Web UI).

You can also configure keen-pbr manually by modifying the configuration file: `/opt/etc/keen-pbr/config.json`.

### Choose RAW or mangle PREROUTING

This choice exists only on Keenetic / NetCraze. keen-pbr sets {{< term "fwmark" >}} in one of two firewall tables:

- **{{< term "mangle" >}}**: Keenetic rebuilds this table from time to time (for example after a DHCP lease renewal), and keen-pbr's rules are briefly missing until keen-pbr restores them;
- **raw**: Keenetic does not touch it, but rules from **Connection Policies** > **Policy Bindings** run later and overwrite keen-pbr's fwmark.

See also the [Keenetic Community forum thread](https://forum.keenetic.ru/topic/20631-keenetic-ndms-%D0%BE%D1%87%D0%B8%D1%89%D0%B0%D0%B5%D1%82-iptables-%D0%B2%D0%BE-%D0%B2%D1%80%D0%B5%D0%BC%D1%8F-%D0%B7%D0%B0%D0%BF%D1%83%D1%81%D0%BA%D0%B0-%D0%BF%D1%80%D0%BE%D0%B3%D1%80%D0%B0%D0%BC%D0%BC%D1%8B/) about Keenetic rebuilding iptables.

| Mode | Benefit | Trade-off |
|---|---|---|
| **RAW** (default when available) | PBR remains active across a Keenetic mangle rebuild. Long-running, sensitive flows such as gaming sessions avoid rebuild-related breaks, and packets have fewer chances to bypass the VPN. | After keen-pbr sets its `fwmark` in RAW, Keenetic's **Connection Policies** > **Policy Bindings** rules overwrite it. RAW mode therefore works only for devices assigned the **Default policy**. |
| **mangle** | keen-pbr runs after Keenetic sets its own fwmarks for Connection Policy, so it can override routing for devices with a non-default policy. | When Keenetic rebuilds its `mangle` table (usually about every five minutes), keen-pbr rules disappear briefly. A few packets can use the ordinary route before keen-pbr restores them, disrupting a gaming session or another sensitive flow. |

{{< callout type="info" >}}
For the most predictable Keenetic setup, do not assign **Connection Policies** to devices in Keenetic. Instead, create the equivalent per-device [keen-pbr route rules]({{< relref "/docs/configuration/route-rules" >}}) using the `src_addr` (**Source address**) field. This keeps routing policy in keen-pbr and avoids conflicts with Keenetic policy bindings.
{{< /callout >}}

By default keen-pbr checks separately for IPv4 and IPv6 whether RAW is available, and uses it if so; otherwise mangle. The system log shows the selected mode after the service starts:

```text
IPv4 raw PREROUTING available
WARNING: IPv6 raw PREROUTING unavailable: ...
```

To always use mangle, add this line to `/opt/etc/keen-pbr/defaults`:

```sh
KEEN_PBR_RAW_PREROUTING="disable"
```

and restart the service:

```sh
/opt/etc/init.d/S80keen-pbr restart
```

The other values of this variable are described in [CLI]({{< relref "/docs/cli#--use-raw-prerouting" >}}).

Basic commands:

| Action | Command |
| --- | --- |
| Start service | `/opt/etc/init.d/S80keen-pbr start` |
| Restart service | `/opt/etc/init.d/S80keen-pbr restart` |
| Check if **keen-pbr** alive | `/opt/etc/init.d/S80keen-pbr status` |

{{< callout type="info" >}}
If you do not plan to use the keen-pbr Web UI or API, consider installing the `keen-pbr-headless` package.
It uses less storage space (~1.2 MB instead of ~2.8 MB) and does not include the API server at all. You can also disable the API server at any time via a config flag, even when using the full package version.
{{< /callout >}}

### Next steps

Go to the [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) page and use the **Web UI** tab for the easiest initial setup. If you installed the `headless` version, see the **JSON / CLI** tab instead.


{{% /steps %}}

## Upgrading from the dnsmasq integration

Older keen-pbr versions always added their own block to `/opt/etc/dnsmasq.conf`. The current version does not need it, and on upgrade the package cleans up by itself:

- removes the keen-pbr block from `/opt/etc/dnsmasq.conf`, keeping a copy of the original file as `/opt/etc/dnsmasq.conf.keen-pbr.bak`;
- if dnsmasq would be left without any DNS server to forward to, adds a `# BEGIN keen-pbr fallback upstream` block with public DNS servers. **Review it** and replace it with your own servers if needed;
- restarts dnsmasq once.

You can also remove {{< term "Entware" >}} dnsmasq altogether and use the built-in Keenetic DNS proxy: keen-pbr works with any resolver.

[Per-list DNS servers]({{< relref "/docs/configuration/dns#per-list-dns-servers-dnsmasq" >}}) need Entware `dnsmasq-full` as the LAN resolver. When you enable them, keen-pbr adds a `# BEGIN keen-pbr upstream dns` block to `/opt/etc/dnsmasq.conf`.
