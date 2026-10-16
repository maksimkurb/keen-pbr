---
title: Keenetic / NetCraze
weight: 1
---

On Keenetic and NetCraze, keen-pbr is installed from {{< term "Entware" >}}. Entware is a set of extra programs for the router, installed on a USB drive. Do use a USB drive: that way installing packages and running programs does not wear out the router's built-in memory. You install programs from Entware with the `opkg` command over SSH.

{{% steps %}}

### Install Entware

If Entware is already on your router, go to the next step.

Installation instructions are in the [Keenetic](https://support.keenetic.com/?lang=en) or [NetCraze](https://support.netcraze.ru/?lang=en) knowledge base:

1. Find the page for your router model.
2. Search for "Installing the Entware repository on a USB drive".
3. Follow the instructions step by step. They also explain how to connect to the router over SSH, which you need later.

### Install KeeneticOS components

Open the router's components page in a browser: `http://my.keenetic.net/system/osAndUpdates` (or `http://<router-IP>/system/osAndUpdates`). Install these components:

- Network Functions / **IPv6 Protocol**. NDMS 5.0 and newer do not have this item: the protocol is built in.
- OPKG Packages / **Open Package System Support**
- OPKG Packages / **Netfilter Subsystem Kernel Modules**
- OPKG Packages / **Xtables-addons Extension Package for Netfilter**
- Utilities and services / **DNS-over-TLS proxy** (optional, but recommended)
- Utilities and services / **DNS-over-HTTPS proxy** (optional, but recommended)

### Turn on DNS encryption (recommended)

{{< term "DoH" >}} (DNS-over-HTTPS) and DoT (DNS-over-TLS) send DNS requests in encrypted form. Your provider cannot tamper with the answers, so sites open at the correct addresses.

How to set it up: [DoH and DoT proxy servers for DNS requests encryption](https://support.keenetic.com/titan/kn-1811/en/25049.html).

### Install keen-pbr

1. Open the [keen-pbr repository page](https://repo.keen-pbr.fyi/repository/stable/?lang=en).
2. On the left, select the OS **Keenetic / NetCraze**, the OS version **current** (Keenetic has only this one) and the architecture of your router.
    - To find the architecture, run `opkg print-architecture` over SSH.
3. Connect to the router over SSH and run the commands from the repository page, in order.
4. Install the package:
    ```bash {filename="bash"}
    opkg install keen-pbr

    # version without the web UI and API:
    # opkg install keen-pbr-headless
    ```

At the end of the installation you will see `Installation complete!` and the web UI address.

{{< callout type="info" >}}
`keen-pbr-headless` takes less space: about 1.2 MB instead of 2.8 MB. It has no web UI and no API, so you configure it through the config file. If in doubt, install the regular `keen-pbr`.
{{< /callout >}}

### Check that the service is running

Run over SSH:

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr status
```

If everything is fine, you will see `alive`:

```text
~ # /opt/etc/init.d/S80keen-pbr status
 Checking keen-pbr...              alive.
```

If it says `dead` instead, open [Service does not start]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

### Open the web UI

Open http://my.keenetic.net:12121 in your browser. If the address does not open, enter `http://<router-ip>:12121`, for example `http://192.168.1.1:12121`.

The headless version has no web UI. Its configuration is the file `/opt/etc/keen-pbr/config.json`.

### Set up your first rules

Go to the [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}) page and open the **Web UI** tab. If you installed `keen-pbr-headless`, open the **JSON** tab.

{{% /steps %}}

## Service commands

| Action | Command |
| --- | --- |
| Start | `/opt/etc/init.d/S80keen-pbr start` |
| Restart | `/opt/etc/init.d/S80keen-pbr restart` |
| Check that it works | `/opt/etc/init.d/S80keen-pbr status` |

## RAW or mangle

You do not need to change anything at first: keen-pbr picks a suitable mode itself. This section helps if you assign policies to devices in Keenetic **Connection Policies**, or if connections through the VPN sometimes drop.

keen-pbr marks the packets that must go to a VPN or another channel. This mark is called {{< term "fwmark" >}}. It can be set in one of two firewall tables of the router: RAW or {{< term "mangle" >}}.

| Mode | Benefit | Trade-off |
|---|---|---|
| **RAW** (default when the router supports it) | Keenetic does not rebuild this table, so keen-pbr rules work without gaps. Gaming sessions and other long connections do not break, and traffic does not bypass the VPN. | Policies from **Connection Policies** > **Policy Bindings** run later and overwrite the keen-pbr mark. RAW therefore works only for devices with the **Default policy**. |
| **mangle** | keen-pbr runs after Keenetic policies and can change the route for devices with any policy. | Keenetic rebuilds the `mangle` table (about every five minutes, and also, for example, after a DHCP lease renewal). During that time keen-pbr rules are missing. A few packets can take the ordinary route, and a sensitive connection such as a gaming session can break. |

More about how Keenetic rebuilds iptables: [Keenetic Community forum thread](https://forum.keenetic.ru/topic/20631-keenetic-ndms-%D0%BE%D1%87%D0%B8%D1%89%D0%B0%D0%B5%D1%82-iptables-%D0%B2%D0%BE-%D0%B2%D1%80%D0%B5%D0%BC%D1%8F-%D0%B7%D0%B0%D0%BF%D1%83%D1%81%D0%BA%D0%B0-%D0%BF%D1%80%D0%BE%D0%B3%D1%80%D0%B0%D0%BC%D0%BC%D1%8B/).

{{< callout type="info" >}}
The simplest way is not to assign policies to devices in **Connection Policies**. Instead, create a keen-pbr [route rule]({{< relref "/docs/configuration/route-rules" >}}) and enter the device address in the **Source IP** field. Then all rules are in one place and do not conflict with Keenetic policies.
{{< /callout >}}

The mode is chosen separately for IPv4 and IPv6. The system log of the router shows which one was chosen after the service starts:

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

The other values of this setting are described in [CLI]({{< relref "/docs/cli#--use-raw-prerouting" >}}).

## Separate DNS servers for lists (dnsmasq) {#dnsmasq}

keen-pbr does not need dnsmasq for routing. Install it only if you want to send the domains of some lists to separate DNS servers, or set your own default DNS servers. See [Do I need dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Install dnsmasq {#dnsmasq-install}

1. Install the package over SSH. Entware has only `dnsmasq-full`; the name `dnsmasq` resolves to it:
   ```bash {filename="bash"}
   opkg install dnsmasq-full
   ```
2. To apply the recommended dnsmasq settings, run this over SSH:
   ```bash {filename="bash"}
   /opt/usr/lib/keen-pbr/dnsmasq-hook.sh install-recommended-config
   ```
   It replaces `/opt/etc/dnsmasq.conf` and saves the previous file as `/opt/etc/dnsmasq.conf.backup-keenpbr`. `my.keenetic.net` keeps resolving.
3. Open http://my.keenetic.net/a (if the link does not open, use `http://<router-ip>/a`) and run:
   ```
   opkg dns-override
   system configuration save
   ```
   Run these commands only after dnsmasq is installed. Without dnsmasq, `dns-override` leaves the LAN without DNS.
4. Reboot the router. **This is required:** without a reboot LAN clients keep using the built-in DNS proxy, and dnsmasq does not get their queries.
5. In the Web UI, open **General** > **Settings** > **Local DNS server management** and turn on **Manage dnsmasq configuration**. The DNS servers and DNS rules pages become available.

### Return to how it was {#dnsmasq-rollback}

Do the steps in this order, so the LAN keeps DNS all the time:

1. In the Web UI, turn off **Manage dnsmasq configuration** (**General** > **Settings** > **Local DNS server management**) and apply the config. dnsmasq keeps running and uses the servers from `/opt/etc/keen-pbr/dnsmasq-fallback.conf`, so DNS keeps working. If dnsmasq has servers of its own, these are added to them; clear the file to avoid that.
2. In the web CLI (http://my.keenetic.net/a) run:
   ```
   no opkg dns-override
   system configuration save
   ```
3. Reboot the router. **This is required.**
4. Remove the package:
   ```bash {filename="bash"}
   opkg remove dnsmasq-full
   ```

To keep dnsmasq with its previous settings after `install-recommended-config`, put the saved file back instead of removing the package:

```bash {filename="bash"}
cp /opt/etc/dnsmasq.conf.backup-keenpbr /opt/etc/dnsmasq.conf
/opt/etc/init.d/S56dnsmasq restart
```
