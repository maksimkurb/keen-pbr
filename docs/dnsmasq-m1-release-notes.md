# dnsmasq integration (M1): release notes

## Unreleased

### Behaviour change: a stopped dnsmasq is no longer restarted

keen-pbr no longer restarts a stopped dnsmasq. If dnsmasq crashes on a system without a service supervisor (Entware), start it manually; keen-pbr re-applies its configuration once dnsmasq is running again.

### New

- **Instance selection.** keen-pbr manages one dnsmasq instance, identified by a selector (`uci:<section>`, `entware:default`, `systemd:<unit>.service`). Set it with `dns.dnsmasq_instance`; when empty, keen-pbr keeps using the instance where its drop-in is already installed, or picks the only enabled instance with DNS. An ambiguous choice changes nothing and reports `DNSMASQ_INSTANCE_AMBIGUOUS`.
- **DNS endpoint detection.** The compile-time `127.0.0.1:53` is gone. keen-pbr reads the configuration the dnsmasq process loaded, finds its DNS sockets in `/proc` and confirms the endpoint with a query. The routing test uses the detected endpoint.
- **Safe apply.** Before a restart keen-pbr checks that this dnsmasq build runs `conf-script` under `--test` and runs `dnsmasq --test` on the real configuration plus the new drop-in. A rejected configuration never restarts dnsmasq. Side effect: the generator runs once more for every preflight, so syslog shows an extra `generate-resolver-config dnsmasq: pid ..., parent ... (dnsmasq)` block before each apply.
- **Error codes.** `/api/health/service` and the Web UI report the dnsmasq instance, endpoint and a `DNSMASQ_*` error code; see [DNS configuration](content/docs/configuration/dns.md).
- **CLI.** `keen-pbr dnsmasq detect [--json]` lists instances and the selection; `keen-pbr dnsmasq remove <selector>` removes keen-pbr's drop-in and owned settings from one instance. Disabling the integration while keen-pbr is stopped no longer cleans up automatically; leftovers are listed by `detect` and removed with `remove`.
- **Web UI.** A dnsmasq instance field in the DNS settings.

### OpenWrt: `addnmount` ownership

keen-pbr records every `addnmount` entry it adds in the list `dhcp.<section>.keen_pbr_addnmount` and removes only those entries. Entries that existed before this release (including ones added by earlier keen-pbr versions) are never claimed and stay in place; remove them by hand if you no longer need them.

### Hook protocol v2

The platform hooks (OpenWrt, Keenetic, Debian) now require a selector for `apply` and `remove` and gained an `inspect` command. An old daemon calling a new hook (and a new daemon calling an old hook) fails loudly with a usage error instead of touching every dnsmasq instance. Upgrade the keen-pbr package as a whole and restart the service.

### Known gaps

- The M0 experiments (`docs/dnsmasq-m0-results.md`) have not been run on real hardware yet. The defaults from the specification are used: `dnsmasq --test` is assumed to execute `conf-script`, and the OpenWrt single-instance restart and Keenetic/Debian instance identification follow the documented behaviour of `dnsmasq.init`, not measured results. Hardware QA is still pending.
- On the development machine, the test that applies a real dnsmasq config is skipped because of an environment restriction on reading config files.
