---
title: DNS
weight: 4
---

Domains from your lists work without any DNS setup. keen-pbr itself reads the plain DNS replies (port 53) that pass through the router, and the site names at the start of TLS, HTTP and QUIC connections (this is called L7: the site name is read from the first packets of a connection). Details are in [DNS interception](#dns-interception).

## Do I need dnsmasq? {#do-i-need-dnsmasq}

**No, not for routing.** Interception works with any resolver: the Keenetic DNS proxy, unbound, dnsmasq or anything else. If your ISP spoofs DNS answers, DoH/DoT on the router is usually enough (DoH and DoT are encrypted DNS).

**Yes, if you need one of these two things:**

- to resolve the domains of some lists through a separate DNS server. For example, a CDN (content delivery network) should answer for your VPN's region, or the domains resolve only inside the VPN;
- to set your own main DNS servers for the whole network right in keen-pbr.

Both work only through dnsmasq. If neither fits you, leave the **Manage dnsmasq configuration** switch off. If you upgraded from 3.3.x, the switch is already on: turn it off if you do not need dnsmasq (see [Breaking Changes in 3.4.0]({{< relref "/docs/changelog#upgrading-from-33x" >}})).

How to install dnsmasq and how to roll back: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq" >}}).

## How to check that DNS goes through the router

Interception sees only the DNS queries that pass through the router. If a device uses DoH or its own DNS server, keen-pbr can recognise part of the domains only through L7.

1. Open **General** > **Dashboard** and find the **DNS interception** card.
2. Click **Check again**. To test another device, click **Test from another device** and run the shown `nslookup` command on a computer or phone while the window is open.

You can also check by hand on the device:

```bash {filename="bash"}
nslookup check.keen-pbr.fyi
```

- Answer `127.0.0.88`: the device's DNS queries go through the router, everything works.
- Any other address: the device bypasses the router or uses encrypted DNS. Turn off secure DNS (DoH) in the browser and the system.
- No answer: DNS does not work on the device.

## Per-list DNS servers (dnsmasq) {#per-list-dns-servers-dnsmasq}

Use this when a CDN should return addresses for your VPN's region, or when different services need different DNS servers.

**Example**: AI service domains are resolved through a DNS server behind the VPN. The CDN then returns addresses for the VPN exit country, and the service does not see that you connect from another country (otherwise the account may get banned).

{{% steps %}}

### Turn on dnsmasq management

Open **General** > **Settings** > **Local DNS server management** and turn on **Manage dnsmasq configuration**. The **DNS servers** and **DNS Rules** pages open only after that.

### Add a DNS server

Open **DNS** > **DNS servers** > **Add DNS server**. Fill in **Name**, **DNS type** and **Address**. In **Make requests via Outbound** pick an outbound (the channel the traffic goes through: a VPN interface, your ISP or a block) if the server is reachable only through it, for example through the VPN.

### Link the server to lists

Open **DNS** > **DNS Rules** > **Add DNS rule**. In **Domain lists** pick the lists, and in **DNS servers** the server from the previous step. You can pick several servers.

### Set main DNS servers (optional)

On the **DNS** > **DNS servers** page, find the **Main DNS servers** block under the table, click **Add main DNS server** and pick the servers for all other domains. If the block is empty, dnsmasq uses its usual DNS servers.

### Apply the settings

Click **Apply** (or **Apply & Restart**).

{{% /steps %}}

The same settings in `config.json`:

```json { filename="config.json" }
{
  "dns": {
    "resolver_integration": "dnsmasq",
    "servers": [
      {"tag": "vpn_dns", "address": "1.1.1.1", "detour": "vpn"},
      {"tag": "isp_dns", "address": "192.168.1.1"}
    ],
    "rules": [
      {
        "list": ["ai_services"],
        "servers": ["vpn_dns"],
        "allow_domain_rebinding": false
      }
    ],
    "fallback": ["isp_dns"]
  }
}
```

**Several DNS servers for one list.** Pick two or more servers in the **DNS servers** field of the rule. Requests for the list's domains go to the servers in turn. If a server does not answer, the next one takes over, and the silent one is skipped for a few seconds.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {"tag": "vpn_dns", "address": "10.8.0.1", "detour": "vpn"},
      {"tag": "vpn_dns_2", "address": "10.8.0.2", "detour": "vpn"}
    ],
    "rules": [
      {"list": ["ai_services"], "servers": ["vpn_dns", "vpn_dns_2"]}
    ]
  }
}
```

## DNS servers

A server name (`tag`) must be unique: lowercase Latin letters, digits and `_`, starts with a letter, at most 24 characters.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Server name (**Name**) |
| `type` | string | no | Server type (**DNS type**):<ul><li>`static` (**Plaintext DNS**, default)</li><li>`doh` (**DNS-over-HTTPS**)</li><li>{{< only keenetic >}} `keenetic` (**Keenetic DNS**)</li></ul> |
| `address` | string | for `static` and `doh` | `static`: IP address of the DNS server, with optional port, for example `"10.8.0.1"`, `"10.8.0.1:5353"`, `"2001:4860:4860::8888"`, or `"[2001:4860:4860::8888]:5353"`. `doh`: the server URL starting with `https://`, for example `"https://dns.google/dns-query"` (**URL**) |
| `detour` | string | no | Outbound the router itself uses to send queries to this server (**Make requests via Outbound**) |
| `ips` | array of string | no | Only for `doh`: IP addresses of the server, without a port (**Server IP addresses**). When set, keen-pbr connects only to these addresses. The server certificate is still checked against the name in `address`. See [Finding the address of a DoH server](#doh-bootstrap) |

`detour` is useful when a DNS server must be reached through a specific connection, usually the same VPN. It does not affect queries that devices in your network send directly to the same address: route them with a normal route rule.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "type": "static",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      },
      {
        "tag": "google_doh",
        "type": "doh",
        "address": "https://dns.google/dns-query"
      },
      {
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ]
  }
}
```

### The "DNS-over-HTTPS" type {#dns-over-https}

DNS-over-HTTPS (DoH) sends requests to the server over an encrypted HTTPS connection, so your ISP cannot read or spoof the answers. Pick **DNS-over-HTTPS** as **DNS type** and enter the server address in **URL**, for example `https://dns.google/dns-query` or `https://cloudflare-dns.com/dns-query`.

- Use the server in a DNS rule or among the main DNS servers like any other one. Several DoH servers, or a DoH server together with plain ones, can be picked in one rule.
- **Make requests via Outbound** works as for other servers: keen-pbr connects to the DoH server through the chosen outbound.
- The router must have CA certificates installed to check the server (Keenetic: the `ca-certificates` package in Entware; OpenWrt: `ca-bundle`; Debian: `ca-certificates`). If it has none, the status page shows the problem "DNS-over-HTTPS servers are not used".

### Finding the address of a DoH server {#doh-bootstrap}

When the URL of a DoH server has a host name (`dns.google`) and not an IP address, keen-pbr first asks a plain DNS server for the address of that name. These servers are called bootstrap servers.

By default keen-pbr asks the first plain server of the **DNS servers** table. To choose other ones, open **DNS** > **DNS servers**, find the **Bootstrap servers** block under **Main DNS servers**, and click **Add bootstrap server**. Plain DNS servers and Keenetic DNS fit. The servers are asked in the order of the list. If a server does not answer, answers with an error, says the name does not exist, or gives no address, keen-pbr asks the next one. After you change the list, the addresses are looked up again.

If your ISP blocks the name of a DoH server, put first a bootstrap server that does not block it, for example one reached through a VPN (**Make requests via Outbound**).

If you know the addresses of the server, enter them in the **Server IP addresses** field of the DoH server, separated by commas. keen-pbr then connects only to them and does not look the server up.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {"tag": "google_dns", "address": "8.8.8.8"},
      {"tag": "quad9", "address": "9.9.9.9"},
      {"tag": "google_doh", "type": "doh", "address": "https://dns.google/dns-query"},
      {
        "tag": "cloudflare_doh",
        "type": "doh",
        "address": "https://cloudflare-dns.com/dns-query",
        "ips": ["1.1.1.1", "1.0.0.1"]
      }
    ],
    "bootstrap": ["google_dns", "quad9"]
  }
}
```

### The "Keenetic DNS" type

{{< only keenetic >}}\
On Keenetic routers, `type: "keenetic"` takes the DNS servers from the router's current settings. You set them in the Keenetic web interface: **Network rules** > **Internet safety** > **DNS Configuration**. If DoT or DoH servers are set there, only they are used.

- `address` is not set for it.
- keen-pbr reads the servers that are not tied to a domain from the **System** DNS proxy group. If any of them are encrypted ({{< term "DoH" >}}/DoT), all encrypted servers are used in order. Otherwise, all plain servers are used in order.

### Checking the DNS servers {#check-dns-servers}

The **Check** button on **DNS** > **DNS servers** asks every server in the list about a few sites and shows which servers work and which do not. The servers from the applied settings are checked, even when no rule uses them. If the web interface has changes you have not applied, a hint appears above the result.

The arrow next to the button opens a menu of sites. `yandex.ru` and `telegram.org` are selected by default; you can add `youtube.com`, `google.com`, `instagram.com`, `chatgpt.com`, or type your own site into **Another site**. One check asks about five sites at most. The browser remembers your choice.

After the check, a summary appears above the table, and each server gets a status in the **Check** column. Click a status to see the server's answer for each site: the addresses and how long it took.

- "Works": the server answered every question.
- "Not answering": the server did not answer in time, refused the connection, is unreachable or returned an error. A DNS-over-HTTPS server without certificates cannot be checked, which also shows as "Not answering" with the reason. Servers with an IPv6 address are skipped while IPv6 is off.
- "Suspicious": the server answers, but the answers look tampered with. The signs are:
  - the server says a well-known site does not exist;
  - the answer holds a stub address instead of the site's address: `0.0.0.0`, `127.0.0.1` or an address from a local network. This is how ISPs and filters block sites;
  - the server answers with addresses for names that do not exist.

For your own site from **Another site**, "does not exist" or "no addresses" stays an ordinary result: keen-pbr cannot know whether the site exists.

If a server tampers with answers, use a DNS-over-HTTPS server, or set an outbound for the server in **Make requests via Outbound** so the requests bypass your ISP.

## Field reference

The `dns` section in `config.json`:

| Field | Type | Required | Description |
|---|---|---|---|
| `servers` | array | no | DNS server definitions (see above) |
| `resolver_integration` | string | no | `"none"` (default) or `"dnsmasq"`. In the Web UI: **Manage dnsmasq configuration**. |
| `dnsmasq_instance` | string | no | Selector of the dnsmasq instance to manage: `uci:<section>` (OpenWrt), `entware:default` (Keenetic), `systemd:<unit>.service` (Debian). In the Web UI: **dnsmasq instance**. Empty means automatic selection; needed only when several instances are found. |
| `rules` | array | no | Rules that map lists to DNS servers (only active when `resolver_integration: "dnsmasq"`) |
| `bootstrap` | array of string | no | Tags of plain DNS servers (`static` or `keenetic`) that find the IP addresses of DoH servers, asked in order. In the Web UI: **Bootstrap servers**. If omitted, the first plain server of `servers` is used. |
| `fallback` | array of string | no | Tags of the main DNS servers, used for queries that match no rule (only active when `resolver_integration: "dnsmasq"`). In the Web UI: **Main DNS servers**. If omitted, dnsmasq keeps its own servers and also gets the servers from `dnsmasq-fallback.conf`. |
| `proxy_address` | string | no | IPv4 address of the built-in DNS proxy, which handles the queries of lists with several DNS servers or with DNS-over-HTTPS. Default `127.0.0.89`. In the Web UI (advanced mode): **DNS proxy address**. Change it only if the address is taken. |
| `proxy_port_start` | integer | no | First port of the DNS proxy, default `52100`. Each set of DNS servers takes the next port, up to 32 ports. In the Web UI (advanced mode): **First DNS proxy port**. |
| `dns_test_server` | object | no | Ignored; use `intercept.dns.marker` |

Rule (`rules[]` element):

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | bool | true | Whether the rule is active |
| `list` | array of string | required | Names of lists whose domains use this DNS server (**Domain lists**) |
| `servers` | array of string | required | Tags of the DNS servers (**DNS servers**). With two or more, requests go to them in turn |
| `allow_domain_rebinding` | bool | false | Allow private IP answers (RFC1918, local network addresses) for these domains → `rebind-domain-ok` in dnsmasq (**Allow domain rebinding for these domains**). Turn it on only if the list points to internal services. |

## DNS interception

Interception is on by default. Settings: **General** > **Settings** > **Domain-based routing**. It learns the IP addresses of domains from your lists:

- **DNS**: keen-pbr briefly holds a DNS reply passing through the router, adds its addresses to the list's set, and only then lets the reply reach the client. This way even the first connection to a site takes the right route.
- **L7**: keen-pbr reads the site name at the start of TLS, HTTP and {{< term "QUIC" >}} connections. This helps when a device uses encrypted DNS. The first connection to the site then breaks, and the browser or app reconnects over the right route.

Change these settings only when the defaults do not suit you. The `intercept` section in `config.json`:

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enable interception (**Enable domain-based routing**) |
| `min_ttl_ms` / `max_ttl_ms` | integer | `300000` / `86400000` | How long to keep an address in a dynamic set: no less and no more than this, in milliseconds (rounded to seconds). Within these bounds the {{< term "TTL" >}} from the DNS reply is used (**Minimum TTL**, **Maximum TTL**) |
| `dns.enabled` | boolean | `true` | Read DNS replies, UDP and TCP (**Intercept DNS server responses**) |
| `dns.queue_num` | integer | `9053` | {{< term "NFQUEUE" >}} number. Change it only if another program uses it (**NFQUEUE number**) |
| `dns.hold_timeout_ms` | integer | `30` | How long to wait for addresses to be written before passing the DNS reply to the client. If time runs out, the reply goes out without waiting (**DNS hold timeout**) |
| `dns.marker` | object | `check.keen-pbr.fyi` / `127.0.0.88` | Test domain: if the client's DNS goes through the router, it resolves to `127.0.0.88` (**Marker domain**, **Marker IPv4 address**) |
| `l7.enabled` | boolean | `true` | Read site names in connections (**Enable L7 interception**) |
| `l7.nflog_group` | integer | `9054` | {{< term "NFLOG" >}} group number. Change it only if another program uses it (**NFLOG group**) |
| `l7.tls` / `http` / `quic` | boolean | `true` | Which protocols to read (**TLS SNI**, **HTTP Host**, **QUIC**) |

### Kernel requirements

Interception works on most routers. Minimum Linux kernel versions:

| Firewall backend | DNS interception | L7 interception |
|---|---|---|
| iptables | 3.8 | 2.6.39 |
| nftables | 4.1 | 4.5 |

Router vendors sometimes strip parts of the kernel, so keen-pbr does not rely on the version number: when the service starts, it checks that everything it needs actually works. If something is missing, only that part of interception is turned off and the rest of keen-pbr keeps working. The result is shown on the status page of the web UI and in `GET /api/health/service` (the `intercept` field). The check runs once at startup: if you install missing kernel modules, restart the service.

## If you use dnsmasq: installation and problems

### Installation by platform

keen-pbr does not install dnsmasq; install it yourself when you need this feature. Step-by-step install and rollback are on the installation pages: [Keenetic]({{< relref "/docs/getting-started/installation/keenetic#dnsmasq" >}}), [OpenWrt]({{< relref "/docs/getting-started/installation/openwrt#dnsmasq" >}}), [Debian]({{< relref "/docs/getting-started/installation/debian#dnsmasq" >}}). What keen-pbr changes on each platform:

**OpenWrt**: install `dnsmasq`. keen-pbr adds three `addnmount` entries to the dnsmasq section in `/etc/config/dhcp`, so that the sandboxed dnsmasq can run keen-pbr. While management is on and main DNS servers are set, keen-pbr takes the servers without a domain out of `/etc/config/dhcp`; per-domain servers stay. Turning management off or clearing the main servers returns them. Removing the package removes the added entries.

**Keenetic (Entware)**: install `dnsmasq-full` (replaces the built-in ndnproxy as the LAN resolver). The first time you turn management on, keen-pbr adds a marked block with one `conf-script` line to `/opt/etc/dnsmasq.conf`. `install-recommended-config` adds the same block. If dnsmasq is not installed, the status page shows an error.

**Debian**: install `dnsmasq`. The first time you turn management on, keen-pbr adds a marked block to `/etc/dnsmasq.conf`. `install-recommended-config` adds the same block. If dnsmasq is not installed, the status page shows an error.

Keenetic and Debian also ship `install-recommended-config` (see the installation pages). It replaces the main dnsmasq config with recommended settings, saves the previous file next to it, and restarts dnsmasq.

### Choosing the dnsmasq instance

A router can run several dnsmasq instances (on OpenWrt, one per `dnsmasq` section in `/etc/config/dhcp`). keen-pbr manages exactly one of them and identifies it by a **selector**:

| Platform | Selector | Example |
|---|---|---|
| OpenWrt | `uci:<section>` (name as shown by `uci show dhcp`) | `uci:cfg01411c`, `uci:guest` |
| Keenetic ({{< term "Entware" >}}) | `entware:default` | `entware:default` |
| Debian | `systemd:<unit>.service` | `systemd:dnsmasq.service` |

The instance is picked in this order:

1. **Explicit**: `dns.dnsmasq_instance` is set. If no such instance exists, keen-pbr reports `DNSMASQ_INSTANCE_NOT_FOUND` and changes nothing.
2. **Adopted**: the field is empty and keen-pbr's {{< term "drop-in" >}} is already installed in exactly one instance. That instance keeps being used. If the drop-in is found in several instances, selection stops with `DNSMASQ_INSTANCE_AMBIGUOUS`.
3. **Automatic**: the field is empty, nothing is installed yet, and exactly one enabled instance has DNS (`port` is not `0`). With none, the result is `DNSMASQ_NO_DNS_INSTANCE`; with several, `DNSMASQ_INSTANCE_AMBIGUOUS`.

When the choice is ambiguous, keen-pbr does not guess: it modifies nothing and leaves existing drop-ins alone. Run `keen-pbr resolver dnsmasq detect` to see the candidates, then set it under **General** > **Settings** > **Local DNS server management** > **dnsmasq instance** (or `dns.dnsmasq_instance` in `config.json`). If keen-pbr's drop-in is also installed in an instance that is not selected, `detect` and the logs show a warning. Delete the drop-in file named in the warning and restart that dnsmasq instance.

#### DNS endpoint detection

keen-pbr finds the address the selected dnsmasq answers on by itself: it reads the configuration dnsmasq actually loaded and checks the address with a DNS query. If dnsmasq does not serve DNS (`port=0`, no DNS socket, or another {{< term "network namespace" >}}), keen-pbr reports an error instead of guessing `127.0.0.1:53`.

#### Checks before every restart

Before restarting dnsmasq, keen-pbr checks the new settings with `dnsmasq --test` on your current dnsmasq configuration plus keen-pbr's {{< term "drop-in" >}}. It also checks once that your dnsmasq build supports `conf-script`.

If a check fails, **dnsmasq is not restarted and keeps its old settings**. The error code and the dnsmasq output are shown in the web UI and in `/api/health/service`; fix the problem and apply the configuration again.

Each check runs the config generator, so before each apply the system log shows an extra `generate-resolver-config dnsmasq: pid ..., parent ... (dnsmasq)` block. This is expected.

#### Errors

The code is shown in `GET /api/health/service` (`error_code`) and in the Web UI. In every case below keen-pbr leaves dnsmasq unrestarted unless the table says otherwise.

| Code | Meaning | What to do |
|---|---|---|
| `DNSMASQ_NO_HOOK` | This build has no dnsmasq hook. | Use a package built for your platform. |
| `DNSMASQ_HOOK_FAILED` | The hook's `inspect` call failed or printed something unreadable. | Run `keen-pbr resolver dnsmasq detect`; check that the hook and keen-pbr are from the same version. |
| `DNSMASQ_NOT_INSTALLED` | dnsmasq is not installed. | Install dnsmasq (see above), or, if you do not need it, turn off **Manage dnsmasq configuration** (`dns.resolver_integration: "none"`). |
| `DNSMASQ_NO_DNS_INSTANCE` | No enabled instance serves DNS. | Enable an instance with DNS, or turn off **Manage dnsmasq configuration** (`dns.resolver_integration: "none"`). |
| `DNSMASQ_INSTANCE_NOT_FOUND` | `dns.dnsmasq_instance` names an instance that does not exist. | Fix the selector; `keen-pbr resolver dnsmasq detect` lists valid ones. |
| `DNSMASQ_INSTANCE_AMBIGUOUS` | Several instances qualify. | Set **dnsmasq instance** (`dns.dnsmasq_instance`). |
| `DNSMASQ_NOT_RUNNING` | The selected instance is not running. | Start dnsmasq yourself. keen-pbr never starts it, and re-applies its configuration once dnsmasq runs again. |
| `DNSMASQ_DNS_DISABLED` | The selected instance has `port=0`. | Pick another instance or enable DNS in this one. |
| `DNSMASQ_OTHER_NETNS` | dnsmasq runs in another {{< term "network namespace" >}}. | Not supported; run dnsmasq in the router's main namespace. |
| `DNSMASQ_NO_DNS_LISTENER` | The process runs but owns no DNS socket. | Check `listen-address`, `interface` and `port` in the dnsmasq configuration. |
| `DNSMASQ_ENDPOINT_UNRESPONSIVE` | Sockets exist, but none answered a DNS query. | Check that dnsmasq works (`nslookup example.com 127.0.0.1`). Not restarted. |
| `DNSMASQ_DROPIN_NOT_LOADED` | The running dnsmasq does not read the directory keen-pbr's {{< term "drop-in" >}} goes to (or a `conf-dir` filter rejects the file name). | Add the directory to the `conf-dir` of that instance, or relax the filter. |
| `DNSMASQ_FOREIGN_DROPIN` | The drop-in file exists but keen-pbr did not write it. | Move or delete the file; keen-pbr never overwrites it. |
| `DNSMASQ_CONF_SCRIPT_UNSUPPORTED` | This dnsmasq build does not run `conf-script`. | Install a dnsmasq build with `conf-script` support (on Keenetic, `dnsmasq-full`). |
| `DNSMASQ_TEST_FAILED` | Even a minimal configuration fails `dnsmasq --test`. | Run `dnsmasq --test` by hand and fix what it reports. |
| `DNSMASQ_PREFLIGHT_FAILED` | The current dnsmasq configuration plus keen-pbr's drop-in fails `--test`. | Read the dnsmasq output in the error detail and fix the configuration. dnsmasq was not touched. |
| `DNSMASQ_APPLY_FAILED` | The hook failed to install the drop-in. | See the hook output in the error detail and the system log. |
| `DNSMASQ_CONFIG_NOT_LOADED` | dnsmasq was restarted, but the new configuration did not show up in time. | Check the dnsmasq log; apply the configuration again. |
| `DNSMASQ_STAMP_MISMATCH` | dnsmasq serves a different configuration than expected (for example, it was restarted by someone else). | keen-pbr repairs this itself within the limits described below. |
| `DNSMASQ_REPAIR_PAUSED` | The automatic repair budget is used up. | Find out why dnsmasq loses the configuration, then apply the configuration or restart keen-pbr. |
| `DNSMASQ_PROBE_FAILED` | The process is alive but stopped answering. | Check dnsmasq. It is not restarted. |

### How it works

One {{< term "drop-in" >}} line in the dnsmasq configuration makes dnsmasq run keen-pbr at every start and get the per-list rules and main DNS servers from it.

- With **Manage dnsmasq configuration** on, dnsmasq is restarted when this data changes (list update, settings change, or once after keen-pbr starts), and only after the checks described in [Checks before every restart](#checks-before-every-restart) have passed.
- With it off, dnsmasq gets the servers from the `dnsmasq-fallback.conf` file (Keenetic: `/opt/etc/keen-pbr/`, Debian and OpenWrt: `/etc/keen-pbr/`). They are added to dnsmasq's own servers; clear the file to avoid that. On OpenWrt the file is empty by default.
- With it on but no main DNS servers set, dnsmasq also gets the servers from `dnsmasq-fallback.conf`.

### On stop, disable, or uninstall

- **Stopping keen-pbr**: dnsmasq is not restarted; per-list DNS servers keep working, also after a dnsmasq or router restart.
- **Disabling the feature** (`resolver_integration: "none"`): keen-pbr restarts dnsmasq, which then uses the servers from `dnsmasq-fallback.conf`. On OpenWrt the server list returns to `/etc/config/dhcp`. If you turned it off by editing the config while keen-pbr was stopped, the first start restarts dnsmasq once. To check the result, see [`resolver dnsmasq detect`]({{< relref "/docs/cli#resolver-dnsmasq-detect" >}}).
- **dnsmasq is not running**: keen-pbr never starts a stopped dnsmasq. If dnsmasq crashes on a system without a service supervisor ({{< term "Entware" >}}), start it manually; keen-pbr re-applies its configuration once dnsmasq is running again.
- **dnsmasq loses the config** (restarted with other settings): keen-pbr restarts it at most 3 times (immediately, then after 5 and 10 minutes) and then pauses automatic repair; `state` in `/api/health/service` shows `reconciling` or `error` with the reason. Applying the configuration or restarting keen-pbr re-arms it. A dnsmasq that is running but does not answer the config check is never restarted.
- **Uninstalling the package**: keen-pbr removes its entry from dnsmasq, returns the OpenWrt server list, and restarts dnsmasq. This works even when keen-pbr itself is broken, because the removal scripts never run it. If dnsmasq would be left without any upstream DNS server (it has `no-resolv` and no `server=` line of its own), the servers from `dnsmasq-fallback.conf` stay in a block marked `keen-pbr fallback upstream` in `/opt/etc/dnsmasq.conf` (Keenetic) or `/etc/dnsmasq.conf` (Debian). Replace them with your own. Firewall rules left behind by a keen-pbr that crashed are removed as well.

### Troubleshooting

See [Per-list DNS servers do not work]({{< relref "/docs/troubleshooting#per-list-dns-servers-do-not-work" >}}).

