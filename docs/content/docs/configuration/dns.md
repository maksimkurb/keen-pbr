---
title: DNS
weight: 4
---

Domain-based routing works without any DNS configuration: keen-pbr reads DNS replies and TLS/HTTP/QUIC traffic itself and fills the sets of your lists (see [DNS interception](#dns-interception)). You do not need to set up a resolver for this: dnsmasq, the Keenetic DNS proxy, unbound or anything else keeps working as you set it up.

The settings on this page are needed in two cases:

- to define DNS servers and the outbound used to reach them (`detour`);
- to resolve domains from selected lists through a separate DNS server, usually one reached over the same VPN. This is the only case where keen-pbr manages dnsmasq, and only when you turn it on.

## Configuration

```json { filename="config.json" }
{
  "dns": {
    "servers": [...]
  }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `servers` | array | no | DNS server definitions |
| `resolver_integration` | string | no | `"none"` or `"dnsmasq"`; when omitted, `"dnsmasq"` is assumed if `rules` is non-empty, otherwise `"none"`. |
| `dnsmasq_instance` | string | no | Selector of the dnsmasq instance to manage: `uci:<section>` (OpenWrt), `entware:default` (Keenetic), `systemd:<unit>.service` (Debian). Empty or omitted means automatic selection; needed only when several instances are found. |
| `rules` | array | no | Rules mapping lists to DNS servers (only active when `resolver_integration: "dnsmasq"`). |
| `fallback` | array of string | no | Upstream DNS server tags for queries that match no rule (only active when `resolver_integration: "dnsmasq"`). |
| `dns_test_server` | object | no | Deprecated and ignored (replaced by `intercept.dns.marker`) |

## Per-list DNS servers (dnsmasq) {#per-list-dns-servers-dnsmasq}

When `resolver_integration` is `"dnsmasq"`, keen-pbr configures dnsmasq so that domains from selected lists are resolved through the DNS servers you choose. Use it when a CDN should return addresses for your VPN's region, or when different services need different DNS servers.

**Example**: AI service domains are resolved through a DNS server behind the VPN. The CDN then returns addresses for the VPN exit country, and the service does not see that you connect from another country (otherwise the account may get banned).

### Configuration example

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
        "server": "vpn_dns",
        "allow_domain_rebinding": false
      }
    ],
    "fallback": ["isp_dns"]
  }
}
```

### Field reference

| Field | Type | Required | Description |
|---|---|---|---|
| `resolver_integration` | string | yes | Set to `"dnsmasq"` to enable dnsmasq integration |
| `servers` | array | yes | DNS server definitions (each has `tag`, optional `address`, optional `detour`) |
| `rules` | array | no | Array of DNS rules; each rule maps lists to a DNS server |
| `fallback` | array | no | Server tags used when no rule matches; if omitted, system/ISP upstreams from resolv.conf are used |

**DNS rule** (`rules[]` element):

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | bool | true | Whether this rule is active |
| `list` | array of string | required | List names whose domains should use this DNS server |
| `server` | string | required | DNS server tag to use for domains in these lists |
| `allow_domain_rebinding` | bool | false | Allow private-IP answers (RFC1918) for these domains → `rebind-domain-ok` in dnsmasq |

**Several DNS servers for one list.** Add one rule per server for the same list. The list's domains get the servers of every enabled rule, in rule order. dnsmasq queries all of them, then keeps using the one that answered first, and switches to another when it stops answering. Rebinding is allowed when any of these rules allows it.

dnsmasq keeps a separate record (about 200 bytes) for every domain and server pair. Each extra server adds that much memory for every domain of the list, so two servers for a list of 10,000 domains take about 4 MB instead of 2 MB.

### Installation by platform

dnsmasq is not installed by keen-pbr; install it yourself when you need this feature.

**OpenWrt**: install `dnsmasq`. keen-pbr adds `addnmount` entries to the dnsmasq section in `/etc/config/dhcp` so that the sandboxed dnsmasq can run keen-pbr. When you disable the feature, keen-pbr removes only the entries it added.

**Keenetic (Entware)**: install `dnsmasq-full` (replaces the built-in ndnproxy as the LAN resolver). keen-pbr adds a marked block with `conf-dir=/tmp/keen-pbr/dnsmasq.d,*.conf` to `/opt/etc/dnsmasq.conf`, and the `S55keen-pbr-dnsmasq` script recreates the file in that directory at boot. If dnsmasq is not installed, the status page shows an error.

**Debian**: install `dnsmasq`. keen-pbr creates `/etc/dnsmasq.d/keen-pbr-upstream-dns.conf` containing a single `conf-script` line.

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

When the choice is ambiguous, keen-pbr does not guess: it modifies nothing and leaves existing drop-ins alone. Run `keen-pbr dnsmasq detect` to see the candidates, then set `dns.dnsmasq_instance` in `config.json` or in the Web UI under DNS settings. If keen-pbr's drop-in is also installed in an instance that is not selected, `detect` and the logs show a warning with the command that removes it (`keen-pbr dnsmasq remove <selector>`).

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
| `DNSMASQ_HOOK_FAILED` | The hook's `inspect` call failed or printed something unreadable. | Run `keen-pbr dnsmasq detect`; check that the hook and keen-pbr are from the same version. |
| `DNSMASQ_NOT_INSTALLED` | dnsmasq is not installed. | Install dnsmasq (see above). It is optional: without it per-list DNS servers are unavailable. |
| `DNSMASQ_NO_DNS_INSTANCE` | No enabled instance serves DNS. | Enable an instance with DNS, or set `dns.resolver_integration` to `none`. |
| `DNSMASQ_INSTANCE_NOT_FOUND` | `dns.dnsmasq_instance` names an instance that does not exist. | Fix the selector; `keen-pbr dnsmasq detect` lists valid ones. |
| `DNSMASQ_INSTANCE_AMBIGUOUS` | Several instances qualify. | Set `dns.dnsmasq_instance`. |
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

keen-pbr never writes the generated dnsmasq config to flash. Instead:
1. keen-pbr generates a config (per-list domain rules, fallback servers, etc.)
2. keen-pbr places a one-line {{< term "drop-in" >}} (`conf-script=keen-pbr generate-resolver-config dnsmasq`) in a directory in RAM (tmpfs)
3. dnsmasq runs that command at start and reads the config from its output
4. dnsmasq is restarted only when the generated config changes (list update, settings change, or once after keen-pbr starts), and only after the checks described in [Checks before every restart](#checks-before-every-restart) have passed

### On stop, disable, or uninstall

- **Stopping keen-pbr**: dnsmasq is NOT restarted; per-list DNS servers keep working.
- **Disabling the feature** (`resolver_integration: "none"`): when you switch it off while keen-pbr runs, the {{< term "drop-in" >}} is removed from the selected instance and that instance is restarted (reverts to system/ISP upstreams). While the feature is off keen-pbr never touches dnsmasq, not even at startup, so if you disable the feature and then restart keen-pbr, or remove the integration from the config while keen-pbr is stopped, the drop-in stays. It is harmless (with the integration off it prints only a comment). `keen-pbr dnsmasq detect` shows such leftovers and `keen-pbr dnsmasq remove <selector>` deletes them. See [`dnsmasq` commands]({{< relref "/docs/cli#dnsmasq-detect-and-remove" >}}).
- **dnsmasq is not running**: keen-pbr never starts a stopped dnsmasq. If dnsmasq crashes on a system without a service supervisor ({{< term "Entware" >}}), start it manually; keen-pbr re-applies its configuration once dnsmasq is running again.
- **dnsmasq loses the config** (restarted by something else, drop-in gone): keen-pbr restarts it at most 3 times (immediately, then after 5 and 10 minutes) and then pauses automatic repair; `state` in `/api/health/service` shows `reconciling` or `error` with the reason. Applying the configuration or restarting keen-pbr re-arms it. A dnsmasq that is running but does not answer the config check is never restarted.
- **Uninstalling the package**: the drop-in and any persistent changes to dnsmasq config are removed from every instance, and dnsmasq is restarted. This works even when keen-pbr itself is broken or misconfigured, because the removal scripts never run it. On {{< term "Entware" >}}, if dnsmasq would be left without any upstream DNS server, the servers from `dnsmasq-fallback.conf` are added to `/opt/etc/dnsmasq.conf` in a block marked `keen-pbr fallback upstream`. Firewall rules left behind by a keen-pbr that crashed are removed as well.

### Troubleshooting

See [Per-list DNS servers do not work]({{< relref "/docs/troubleshooting#per-list-dns-servers-do-not-work" >}}).

## DNS interception

The `intercept` section is enabled by default. It controls how keen-pbr learns the IP addresses of domains from your lists:

- **DNS**: keen-pbr briefly holds a DNS reply passing through the router, adds its addresses to the list's set, and only then lets the reply reach the client. This way even the first connection to a site takes the right route.
- **L7**: keen-pbr reads the site name at the start of TLS, HTTP and {{< term "QUIC" >}} connections. This helps when a device uses encrypted DNS.

Change these settings only when the defaults do not suit you:

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enable interception |
| `min_ttl_ms` / `max_ttl_ms` | integer | `300000` / `86400000` | How long to keep an address in a dynamic set: no less and no more than this, in milliseconds (rounded to seconds). Within these bounds the {{< term "TTL" >}} from the DNS reply is used |
| `dns.enabled` | boolean | `true` | Read DNS replies (UDP and TCP) |
| `dns.queue_num` | integer | `9053` | {{< term "NFQUEUE" >}} number. Change it only if another program uses it |
| `dns.hold_timeout_ms` | integer | `30` | How long to wait for addresses to be written before passing the DNS reply to the client. If time runs out, the reply goes out without waiting |
| `dns.marker` | object | `check.keen-pbr.fyi` / `127.0.0.88` | Test domain: if the client's DNS goes through the router, it resolves to `127.0.0.88` |
| `l7.enabled` | boolean | `true` | Read site names in connections |
| `l7.nflog_group` | integer | `9054` | {{< term "NFLOG" >}} group number. Change it only if another program uses it |
| `l7.tls` / `http` / `quic` | boolean | `true` | Which protocols to read |

The `dns.dns_test_server` field from earlier versions has no effect. To check DNS, use `nslookup check.keen-pbr.fyi` or the DNS check widget in the web UI.

### Kernel requirements

Interception works on most routers. Minimum Linux kernel versions:

| Firewall backend | DNS interception | L7 interception |
|---|---|---|
| iptables | 3.8 | 2.6.39 |
| nftables | 4.1 | 4.5 |

Router vendors sometimes strip parts of the kernel, so keen-pbr does not rely on the version number: when the service starts, it checks that everything it needs actually works. If something is missing, only that part of interception is turned off and the rest of keen-pbr keeps working. The result is shown on the status page of the web UI and in `GET /api/health/service` (the `intercept` field). The check runs once at startup: if you install missing kernel modules, restart the service.

## DNS Servers

A server `tag` must be unique and follow the same rules as outbound tags: lowercase Latin letters, digits and `_`, starts with a letter, at most 24 characters.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `type` | string | no | DNS source type: `static` (default) or `keenetic` |
| `address` | string | for `static` | IP address of the DNS server, with optional port, for example `"10.8.0.1"`, `"10.8.0.1:5353"`, `"2001:4860:4860::8888"`, or `"[2001:4860:4860::8888]:5353"` |
| `detour` | string | no | Outbound to use when contacting this DNS server |

The `detour` field is useful when a DNS server must be reached through a specific connection, usually the same VPN that will carry the matching traffic.

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
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ]
  }
}
```

### `type: keenetic` (built-in router DNS via RCI)

On Keenetic routers, `type: "keenetic"` takes the DNS servers from the router's current settings.

Rules and behavior:

- At most one `dns.servers` entry may use `type: "keenetic"`.
- `address` must not be set for `type: "keenetic"` (resolved from {{< term "RCI" >}}).
- keen-pbr reads `dns_server = ...` entries that are not tied to a domain from the **System** proxy policy.
- If any of them are encrypted ({{< term "DoH" >}}/DoT), all encrypted servers are used in order.
- Otherwise, all plain (unencrypted) servers are used in order.

### How `detour` works

With `detour`, DNS queries that the router itself sends to this server (for example from dnsmasq) go through the given outbound. For example, if `vpn_dns` has `detour: "vpn"`, the router reaches `vpn_dns` through `vpn`.

Queries that devices in your network send directly to the same address are not affected; route them with a normal route rule.

## Complete Example

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
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
      }
    ]
  }
}
```
