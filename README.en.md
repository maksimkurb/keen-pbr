# Keenetic PBR

![workflow status](https://img.shields.io/github/actions/workflow/status/maksimkurb/keenetic-pbr/.github%2Fworkflows%2Fbuild-ci.yml?branch=main)
![release](https://img.shields.io/github/v/release/maksimkurb/keenetic-pbr?sort=date)

> **keenetic-pbr** is not an official product of the **Keenetic** company and is in no way affiliated with it. This package is created by an independent developer and is provided "as is" without any warranty. Any questions and suggestions regarding the package can be submitted to the GitHub Issues page or the Telegram chat: https://t.me/keenetic_pbr.

#### [> README на русском <](./README.md)

**keenetic-pbr** is a policy-based routing package for Keenetic routers.

Project Telegram chat (in Russian): https://t.me/keenetic_pbr

With this package, you can set up selective routing for specified IP addresses, subnets, and domains. This is useful if you need to organize secure access to certain resources or selectively distribute traffic across multiple providers (e.g., traffic to site A goes through one provider, while other traffic goes through another).

The package uses `ipset` to store a large number of addresses in the router's memory without significantly increasing load and `dnsmasq` to populate this `ipset` with IP addresses resolved by local network clients.

To configure routing, the package creates scripts in the directories `/opt/etc/ndm/netfilter.d` and `/opt/etc/ndm/ifstatechanged.d`.

## Features

- Domain-based routing via `dnsmasq`
- IP address-based routing via `ipset`
- Configurable routing tables and priorities
- Automatic configuration for `dnsmasq` lists


## Installation (one-liner)

Connect to your EntWare using SSH and run the following command:

   ```bash
   opkg install curl jq && curl -sOfL https://raw.githubusercontent.com/maksimkurb/keenetic-pbr/refs/heads/main/install.sh && sh install.sh
   ```

## Installation (manual)

1. Go to [releases](https://github.com/maksimkurb/keenetic-pbr/releases) page and copy URL for the latest `.ipk` file
   for your architecture

2. Download the `.ipk` file on your router:
   ```bash
   curl -LO <URL-to-latest-ipk-file-for-your-architecture>
   ```

2. **Install it using OPKG:**

   ```bash
   opkg install keenetic-pbr-*-entware.ipk
   ```

This will install Keenetic PBR and configure it on your router.

## Configuration

The installer replaces original **dnsmasq** configuration file.
A backup of your original file is saved as `/opt/etc/dnsmasq.conf.orig`.

Adjust the configuration in the following files according to your needs:

- **Keenetic-PBR configuration:** `/opt/etc/keenetic-pbr/keenetic-pbr.conf`
- **dnsmasq configuration:** `/opt/etc/dnsmasq.conf`

### 1. Edit `keenetic-pbr.conf`

Open `/opt/etc/keenetic-pbr/keenetic-pbr.conf` and edit as needed.

The main thing you probably want to edit is to change `interface` for routing.

```ini
#---------------------#
#   General Settings  #
#---------------------#
[general]
lists_output_dir = "/opt/etc/keenetic-pbr/lists.d"   # Lists will be downloaded to this folder
dnsmasq_lists_dir = "/opt/etc/dnsmasq.d"             # Downloaded lists will be saved in this directory for dnsmasq
summarize = true                                     # If true, keenetic-pbr will summarize IP addresses and CIDR before applying to ipset
use_keenetic_api = true                              # If true, keenetic-pbr will use Keenetic API to check if interface is online

#-------------#
#   IPSET 1   #
#-------------#
[[ipset]]
ipset_name = "vpn"              # Name of the ipset
ip_version = 4                  # IPv4 or IPv6
flush_before_applying = true    # Clear ipset each time before filling it

   [ipset.routing]
   interfaces = ["nwg1", "nwg2"]   # Where the traffic for IPs in this ipset will be directed
                                   # keenetic-pbr will use first interface that is administratively up.
                                   # If use_keenetic_api is enabled, keenetic-pbr will also check if there is an active connection on this interface.
   
   kill_switch = false  # If kill-switch is turned on and all interfaces all down, traffic to the hosts from ipset will be dropped
   fwmark = 1001        # This fwmark will be applied to packets matching the list criteria
   table = 1001         # Routing table number (ip route table); a default gateway to the specified interface above will be added there
   priority = 1001      # Routing rule priority (ip rule priority); the lower the number, the higher the priority

   # Advanced settings: you can specify custom iptables rules that will be applied for the ipset.
   #   Available variables:
   #   {{ipset_name}} - name of the ipset
   #   {{fwmark}} - fwmark
   #   {{table}} - number of the routing table
   #   {{priority}} - priority of the routing rule
   #
   #[[ipset.iptables_rule]]
   #chain = "PREROUTING"
   #table = "mangle"
   #rule = ["-m", "set", "--match-set", "{{ipset_name}}", "dst,src", "-j", "MARK", "--set-mark", "{{fwmark}}"]

   # List 1 (manual address entry)
   [[ipset.list]]
   name = "local"
   hosts = [
       "ifconfig.co",
       "myip2.ru",
       "1.2.3.4",
       "141.201.11.0/24",
   ]

   # List 2 (from local file)
   [[ipset.list]]
   name = "local-file"
   file = "/opt/etc/keenetic-pbr/my-list.lst"

   # List 3 (download via URL)
   [[ipset.list]]
   name = "remote-list-1"
   url = "https://some-url/list1.lst"  # The file should contain domains, IP addresses, and CIDR, one per line

    # List 4 (download via URL)
   [[ipset.list]]
   name = "remote-list-2"
   url = "https://some-url/list2.lst"

# You can add as many ipsets as you want:
# [[ipset]]
# ipset_name = "direct"
# ...
```

### 2. Download lists

After editing the configuration file, download list files

```bash
keenetic-pbr download
```

### 3. Enable DNS-override

1. Open the following URL in the browser:
   ```
   http://<router-ip-address>/a
   ```
2. Enter the following commands:
   1. `opkg dns-override`
   2. `system configuration save`

### 4. Restart OPKG and verify if routing works

Restart OPKG and ensure policy-based routing is functioning as expected.

## Updating lists
List are updated daily by cron automatically.

You can update lists manually by running the following commands
```
keenetic-pbr download
/opt/etc/init.d/S80keenetic-pbr restart
```

## Troubleshooting

For any issues, verify your configuration files and logs.
Ensure lists are downloaded correctly, and `dnsmasq` is running with the updated configuration.

Before checking the workability on the client machine, you need to clear the DNS cache.
To do this, run the command in the console (for Windows): `ipconfig /flushdns`.

You can also run the following command to check if Keenetic-PBR is working correctly (output of this command will be very helpful if you ask for help in the Telegram chat):
```bash
keenetic-pbr self-check
```

You can ask questions in the Telegram chat of the project: https://t.me/keenetic_pbr

---

Enjoy seamless policy-based routing with Keenetic-PBR!
