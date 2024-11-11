# Keenetic PBR

![workflow status](https://img.shields.io/github/actions/workflow/status/maksimkurb/keenetic-pbr/.github%2Fworkflows%2Fbuild-ci.yml?branch=main)
![release](https://img.shields.io/github/v/release/maksimkurb/keenetic-pbr)

Keenetic PBR is a utility for policy-based routing on Keenetic routers, enabling you to direct traffic according to
specific routing policies based on domain lists and IP sets.

It is configurable to integrate with `dnsmasq` for
domain-based routing and utilizes `ipset` for routing control.

## Features

- Domain-based routing via `dnsmasq`
- IP-based routing via `ipset`
- Customizable routing tables and priorities
- Automatic configuration for `dnsmasq` lists

## Installation (one-liner)

Connect to your EntWare using SSH and run the following command:

   ```bash
   opkg install curl jq && curl -sOfL https://raw.githubusercontent.com/maksimkurb/keenetic-pbr/refs/heads/main/install.sh && sh install
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
[general]
# Path to `ipset` binary
ipset_path = "ipset"

# Output directory for routing lists
lists_output_dir = "/opt/etc/keenetic-pbr/lists.d"

# Downloaded lists would be saved to this directory for dnsmasq
dnsmasq_lists_dir = "/opt/etc/dnsmasq.d"

# If true, keenetic-pbr would summarize IPs and CIDRs before applying them to ipset
summarize = true

[[ipset]]
   # Name of the ipset
   ipset_name = "vpn"
   
   # Clear ipset each time before loading it
   flush_before_applying = true

  [ipset.routing]
  # Target interface for routing
  interface = "nwg1"
  
  # This fwmark would apply to all packets with IPs from this ipset
  fwmark = 1001
  
  # ip routing table number
  table = 1001
  
  # ip rule priority number
  priority = 1001

  # Lists to be imported to ipset/dnsmasq
  [[ipset.list]]
  # List 1 name
  name = "list-name"
  # List 1 URL
  url = "https://some-url/list1.lst"

  [[ipset.list]]
  # List 2 name
  name = "re-filter-ipsum"
  # List 2 URL
  url = "https://some-url/list2.lst"
  
# You can add as many ipsets as you want:

# [[ipset]]
#   ipset_name = "direct"
#   flush_before_applying = true
#
#   [ipset.routing]
#   interface = "ppp0"
#   fwmark = 998
#   table = 998
#   priority = 998
#   
#   [[ipset.list]]
#   name = "list-name"
#   url = "https://some-url/list1.lst"
#
#   [[ipset.list]]
#   name = "re-filter-ipsum"
#   url = "https://some-url/list2.lst"
#
# [[ipset]]
#   ...
#
#   [ipset.routing]
#   ...
#  
#   [[ipset.list]]
#   ...
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


---

Enjoy seamless policy-based routing with Keenetic-PBR!
