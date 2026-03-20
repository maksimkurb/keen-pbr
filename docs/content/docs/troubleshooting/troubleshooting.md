---
title: Troubleshooting
weight: 2
---

## Daemon won't start

1. **Check config JSON validity** — parse errors are logged to stderr on startup. Validate with `jq . /etc/keen-pbr/config.json`.
2. **PID file permissions** — ensure the directory for `daemon.pid_file` exists and is writable by the process user.
3. **Cache directory** — ensure `daemon.cache_dir` exists and is writable. Create it if needed: `mkdir -p /var/cache/keen-pbr`.
4. **Port conflict** — if the API is enabled, check that the `api.listen` address/port is not already in use.

## Traffic not routed through VPN

Check the routing health endpoint:

```bash
curl http://127.0.0.1:8080/api/health/routing
```

Look for entries with `"status": "missing"` or `"status": "mismatch"` in:
- `firewall` — the keen-pbr chain may not be hooked into PREROUTING
- `firewall_rules` — fwmark rules for your sets may be missing
- `route_tables` — routing table may not have a default route via the VPN interface
- `policy_rules` — ip rule to lookup the table by fwmark may be missing

Also verify that `fwmark.start`, `fwmark.mask`, and `iproute.table_start` don't conflict with existing rules:

```bash
ip rule list
ip route show table 150
```

## urltest always shows "degraded"

The `urltest` outbound is `degraded` when no child has been successfully selected.

1. Check that the probe `url` is reachable from the outbound interface.
2. Check circuit breaker states via `GET /api/health/service` — if a child shows `"circuit_breaker": "open"`, it is in cooldown. Wait for `circuit_breaker.timeout_ms` to elapse.
3. Check that child outbound interfaces are up: `ip link show tun0`.
4. Reduce `interval_ms` temporarily to force faster re-probing.

## dnsmasq not resolving domains to VPN

1. Verify `generate-resolver-config` produces output: `keen-pbr generate-resolver-config dnsmasq-nftset`.
2. Ensure your dnsmasq configuration includes `conf-script=` pointing to keen-pbr, e.g.: `conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq-nftset`
3. Restart dnsmasq after adding the `conf-script=` line.
4. Check that `dns.rules` lists the correct list names and server tag.
5. If using `detour`, ensure the outbound interface is up.

## Remote list not updating

1. Check keen-pbr logs for HTTP errors when downloading the list.
2. Verify the URL is reachable from the router: `curl -I <url>`.
3. Check `lists_autoupdate.cron` — ensure the schedule is correct.
4. Trigger a manual reload to test: `curl -X POST http://127.0.0.1:8080/api/reload`
5. Check that `daemon.cache_dir` is writable; failed writes prevent caching.

## Port/address filter rules not matching

{{< callout type="warning" >}}
Mixed negation in `src_addr` / `dest_addr` is not supported. All entries in the array must either all start with `!` or none of them should.
{{< /callout >}}

If rules aren't matching as expected:
- Verify that `proto` is set correctly (`"tcp"`, `"udp"`, or `"tcp/udp"`)
- Ensure negated address arrays use `!` on **every** entry, not just some
- Check that the list name in the rule matches exactly (case-sensitive) the key in `lists`

## High fwmark conflicts

If other software on your system uses the same fwmark range, packets may be misrouted or dropped.

Check for conflicts:

```bash
ip rule list
nft list ruleset | grep mark
```

Adjust to a non-conflicting range:

```json
{
  "fwmark": {
    "start": 131072,
    "mask": 16711680
  }
}
```

The `mask` must be exactly two adjacent hex nibbles. `16711680` = `0x00FF0000`, `131072` = `0x00020000`.
