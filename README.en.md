# keen-pbr - Policy Based Routing toolkit for Keenetic routers

![workflow status](https://img.shields.io/github/actions/workflow/status/maksimkurb/keen-pbr/.github%2Fworkflows%2Fbuild-ci.yml?branch=main)
![release](https://img.shields.io/github/v/release/maksimkurb/keen-pbr?sort=date)

> **keen-pbr** is not an official product of the **Keenetic** company and is in no way affiliated with it. This package is created by an independent developer and is provided "as is" without any warranty. Any questions and suggestions regarding the package can be submitted to the GitHub Issues page or the Telegram chat: https://t.me/keen_pbr.

#### [> README на русском <](./README.md)

#### ⚠️For Keenetic router owners without USB port:
* Make sure you've updated to at least version `v-1.3.0-2`
* [Disable lists auto-update](#config-step-3) after package installation/update

This will help prevent excessive wear of the router's NAND-flash memory.

---

**keen-pbr** is a policy-based routing package for Keenetic routers.

Project Telegram chat (in Russian): https://t.me/keen_pbr

With this package, you can set up selective routing for specified IP addresses, subnets, and domains. This is useful if you need to organize secure access to certain resources or selectively distribute traffic across multiple providers (e.g., traffic to site A goes through one provider, while other traffic goes through another).

The package uses `ipset` to store a large number of addresses in the router's memory without significantly increasing load and `dnsmasq` to populate this `ipset` with IP addresses resolved by local network clients.

To configure routing, the package creates scripts in the directories `/opt/etc/ndm/netfilter.d` and `/opt/etc/ndm/ifstatechanged.d`.

## Features

- Domain-based routing via `dnsmasq`
- IP address-based routing via `ipset`
- Configurable routing tables and priorities
- Automatic configuration for `dnsmasq` lists

## How it works

This package contains the following scripts and utilities:
```
/opt
├── /usr
│   └── /bin
│       └── keen-pbr                    # Utility for downloading and processing lists, importing them to ipset, and generating configuration files for dnsmasq
└── /etc
    ├── /keen-pbr
    │   ├── /keen-pbr.conf              # keen-pbr configuration file
    │   ├── /lists.d                    # keen-pbr will place downloaded and local lists in this folder. Don't put anything here yourself, as files from this folder are deleted after each "keen-pbr download" command.
    │   └── /local.lst                  # Default local IP/CIDR/domain list
    ├── /ndm
    │   ├── /netfilter.d
    │   │   └── 50-keen-pbr-fwmarks.sh  # Script adds iptables rule for marking packets in ipset with a specific fwmark
    │   └── /ifstatechanged.d
    │       └── 50-keen-pbr-routing.sh  # Script adds ip rule to direct packets with fwmark to the required routing table and creates it with the needed default gateway
    ├── /cron.daily
    │   └── 50-keen-pbr-lists-update.sh # Script for automatic daily list updates
    └── /dnsmasq.d
        └── (config files)                  # Folder with generated configurations for dnsmasq, making it put IP addresses of domains from lists into the required ipset
```

### Packet routing based on IP addresses and subnets
**keen-pbr** automatically loads IP addresses and subnets from lists into the required `ipset`. Then packets to IP addresses that were added into this `ipset` are marked with a specific `fwmark` and based on routing rules are redirected to a specific interface.

**Process diagram:**
![IP routing scheme](./.github/docs/ip-routing.svg)

### Packet routing based on domains
For domain-based routing, `dnsmasq` is used. Each time local network clients make a DNS request, `dnsmasq` checks if the domain is in the lists, and if it is, adds its IP addresses to `ipset`.

> [!NOTE]  
> For domain routing to work, client devices must not use their own DNS servers. Their DNS server should be the router's IP, otherwise `dnsmasq` won't see these packets and won't add IP addresses to the required `ipset`.

> [!IMPORTANT]  
> Some applications and games use their own methods to obtain IP addresses for their servers. For such applications, domain routing won't work because these applications don't make DNS requests. You'll have to find out the IP addresses/subnets of these applications' servers and add them to the lists manually.

**Process diagram:**
![Domain routing scheme](./.github/docs/domain-routing.svg)

## Router preparation
1. Package functionality has been tested on **Keenetic OS** version **4.2.1**. Functionality on version **3.x.x** is possible but not guaranteed.
2. You need to install additional components on your router in the Management -> System settings section:
   - **Network Functions / IPv6 Protocol**
      - This component is required to install the "Netfilter Subsystem Kernel Modules" component.
   - **OPKG Packages / Open Package System Support**
   - **OPKG Packages / Netfilter Subsystem Kernel Modules**
   - **OPKG Packages / Xtables-addons Extension Package for Netfilter**
      - Currently, this package is not mandatory as its capabilities are not used by keen-pbr, but its features may be useful in the future. Module instructions are [available here](https://manpages.ubuntu.com/manpages/trusty/en/man8/xtables-addons.8.html).
3. You need to install Entware environment on Keenetic ([instructions](https://help.keenetic.com/hc/ru/articles/360021214160)), for this you'll need a USB drive that will be permanently plugged into the router
4. You also need to configure a second (third, fourth, ...) connection through which you want to direct traffic that falls under the lists. This can be a VPN connection or a second provider (multi-WAN).
5. **Your devices must be in the Default Internet Access Policy** (Connection Priorities -> Policy Bindings section). Otherwise, the device may ignore all rules applied by keen-pbr.

## Installation and Update

1. Install necessary dependencies:
   ```bash
   opkg update
   opkg install ca-certificates wget-ssl
   opkg remove wget-nossl
   ```

2. Install opkg repository in the system
   ```bash
   mkdir -p /opt/etc/opkg
   ARCH=$(opkg print-architecture | grep -v 'all' | awk 'NR==1{print $2}' | cut -d'-' -f1)
   echo "Adding keen-pbr repository for architecture \"${ARCH}\""
   echo "src/gz keen-pbr https://maksimkurb.github.io/keen-pbr/${ARCH}" > /opt/etc/opkg/keen-pbr.conf
   ```
   Supported architectures: `mips`, `mipsel`, `aarch64`.

3. Install the package:
   ```bash
   opkg update
   opkg install keen-pbr
   ```

   During installation, the `keen-pbr` package replaces the original **dnsmasq** configuration file.
   A backup will be saved in `/opt/etc/dnsmasq.conf.orig`.

> [!CAUTION]  
> If Entware is installed on the router's internal memory, be sure to [disable list auto-update](#config-step-3) to prevent memory wear!

#### Update:
```bash
opkg update
opkg upgrade keen-pbr
``` 

#### Removal:
```bash
opkg remove --autoremove keen-pbr
```

#### Installed Version Information:
```bash
opkg info keen-pbr
```

## Configuration

Adjust the following configuration files according to your needs (more details below):

1. **(required) [Configure keen-pbr package](#config-step-1)**
   - In this file, you must configure the required ipsets, lists, and output interfaces
2. **(required) [Download remote lists (if you have any)](#config-step-2)**
3. **(optional) [Disable lists auto-update](#config-step-3)**
   - If Entware is installed on internal memory, it is strongly recommended to [disable lists auto-update](#config-step-3) to prevent NAND-flash memory wear
4. **(optional) [Configure DNS over HTTPS (DoH)](#config-step-4)**
   - dnsmasq can be reconfigured for your needs, e.g. you can replace the upstream DNS server with your own
   - It is recommended to install and configure the `dnscrypt-proxy2` package to protect your DNS requests via DNS-over-HTTPS (DoH)
5. **(required) [Enable DNS Override](#config-step-5)**

<a name="config-step-1"></a>
### 1. Configuring keen-pbr package

Open `/opt/etc/keen-pbr/keen-pbr.conf` and edit as needed:

1. You need to adjust the `interfaces` field, specifying the interface through which outgoing traffic that matches the list criteria will go.
2. You also need to add lists (local or remote via URL)

<a name="config-step-2"></a>
### 2. Download lists

After editing the configuration file, enter this command to download list files.

This command must be executed only if you have at least one remote list (the `url` field is specified for some list).

```bash
keen-pbr download
```

<a name="config-step-3"></a>
### 3. Disable lists auto-update
> [!CAUTION]
> If Entware is installed on the router's internal memory, you must disable daily automatic list updates to prevent NAND memory wear.
>
> This command needs to be executed **after each keen-pbr package update**!

To disable list auto-updates, delete the file `/opt/etc/cron.daily/50-keen-pbr-lists-update.sh`:
```bash
rm /opt/etc/cron.daily/50-keen-pbr-lists-update.sh
```

You can always [update lists manually](#lists-update).

<a name="config-step-4"></a>
### 4. Configure DNS over HTTPS (DoH)
> [!TIP]  
> The regular DNS protocol is not secure as all requests are transmitted in plain text.
> This means that ISPs or attackers can intercept and modify your DNS requests ([DNS spoofing](https://en.wikipedia.org/wiki/DNS_spoofing)), directing you to a fake website.
>
> To protect from this, it is recommended to configure the `dnscrypt-proxy2` package, which will use the **DNS-over-HTTPS** (**DoH**) protocol to encrypt DNS requests.
> You can read more about **DoH** [here](https://en.wikipedia.org/wiki/DNS_over_HTTPS).

To configure **DoH** on the router, follow these steps:
1. Download `dnscrypt-proxy2`
    ```bash
    opkg install dnscrypt-proxy2
    ```
2. Edit the file `/opt/etc/dnscrypt-proxy.toml`
    ```ini
   # ... (other config lines here, don't delete them)
   
   # Specify upstream servers (need to remove the hash before server_names)
   server_names = ['adguard-dns-doh', 'cloudflare-security', 'google']
   
   # Specify port 9153 for listening to DNS requests
   listen_addresses = ['[::]:9153']
   
   # ... (other config lines here, don't delete them)
    ```
3. Edit the file `/opt/etc/dnsmasq.conf`
    ```ini
   # ... (other config lines here, don't delete them)
   
   # Change default server 8.8.8.8 to our dnscrypt-proxy2
   server=127.0.0.1#9153
   
   # ... (other config lines here, don't delete them)
    ```

4. Validate configuration syntax
    ```bash
   # Check dnsmasq config
   dnsmasq --test
   # Check dnscrypt-proxy config (run only if you have installed dnscrypt-proxy2)
   dnscrypt-proxy -config /opt/etc/dnscrypt-proxy.toml -check
   ```

<a name="config-step-5"></a>
### 5. Enable DNS Override

To make `dnsmasq` as the main DNS server on the router, you need to enable **DNS Override**.

> [!NOTE]  
> This step is not required if your lists contain only IP addresses or CIDRs and do not specify domain names.

1. Open the following URL in the browser: http://my.keenetic.net/a
2. Enter the following commands one by one:
   - `opkg dns-override`
   - `system configuration save`
3. **Reboot the router!**

> [!TIP]
> If you ever decide to disable DNS Override in the future, execute the following commands: `no opkg dns-override` and `system configuration save`.

### 6. Restart OPKG and verify if routing works

Restart OPKG and ensure policy-based routing is functioning as expected.

To do this, open an address that is not in your lists (e.g., https://www.whatismyip.com) and an address that is in your lists (e.g., https://ifconfig.co) and compare the IP addresses, they should be different.

<a name="lists-update"></a>
## Updating lists

* Lists are updated daily by cron automatically.
   * If you have installed Entware on internal memory, please [disable lists auto-update](#config-step-3) to prevent NAND memory wear.

If you edited the `keen-pbr.conf` settings and want to update lists manually, run the following commands via SSH:

```bash
# Run this if you added new remote lists to download them
keen-pbr download

# Run the following commands to apply new configuration
/opt/etc/init.d/S80keen-pbr restart
/opt/etc/init.d/S56dnsmasq restart
```

## Troubleshooting

For any issues, verify your configuration files and logs.
Ensure lists are downloaded correctly, and `dnsmasq` is running with the updated configuration.

Before checking the workability on the client machine, you need to clear the DNS cache.
To do this, run the command in the console (for Windows): `ipconfig /flushdns`.

You can also run the following command to check if **keen-pbr** is working correctly (output of this command will be very helpful if you ask for help in the Telegram chat):
```bash
# Check dnsmasq configured properly
/opt/etc/init.d/S80keen-pbr check

# Check routing state
/opt/etc/init.d/S80keen-pbr self-check
```

You can ask questions in the Telegram chat of the project: https://t.me/keen_pbr

## I messed everything up, internet is gone

You can temporarily disable this configuration by disabling **OPKG** in the settings (selecting "Not specified" section) and rebooting the router.

If you want to completely remove the package, you need to follow these steps:
1. Execute via SSH: `opkg remove keen-pbr dnsmasq dnscrypt-proxy2`
2. Disable DNS-override (http://my.keenetic.net/a):
   - `no opkg dns-override`
   - `system configuration save`
3. Disable **OPKG** (if you don't use it for other purposes)
4. Reboot the router

### Complete uninstallation

1. Execute via SSH: `opkg remove keen-pbr dnsmasq dnscrypt-proxy2`
2. Disable DNS-override (http://my.keenetic.net/a):
   - `no opkg dns-override`
   - `system configuration save`
3. Disable **OPKG** (if you don't use it for other purposes)
4. Reboot the router

---

Enjoy seamless policy-based routing with **keen-pbr**!