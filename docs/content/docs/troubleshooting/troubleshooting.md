---
title: Troubleshooting
weight: 2
---

Start with the symptom that matches what you see. Each section begins with the simplest checks first, then gives more advanced checks if you still need them.

## Service does not start

1. Confirm that the config file exists in the usual place for your platform.
2. Restart the service with the command from your installation page.
3. If you recently edited the config file, check it for missing commas, broken JSON, or wrong paths.
4. If you are using the full package, make sure you did not accidentally disable the service or replace the config with a headless-only example.

{{% details title="Advanced checks" closed="true" %}}
Use these if the service still will not start:

1. Validate the JSON in your platform's config file, for example: `jq . /etc/keen-pbr/config.json`
2. Make sure the directory for `daemon.pid_file` exists and is writable.
3. Make sure `daemon.cache_dir` exists and is writable.
4. If the API is enabled, make sure the configured listen address and port are not already in use.
{{% /details %}}

## Sites are not going through the VPN

1. Make sure the site is in the correct list.
2. Make sure the route rule for that list points to your VPN outbound.
3. Make sure your VPN connection is actually up.
4. Run a quick test:

```bash {filename="bash"}
keen-pbr test-routing google.com
```

If the expected and actual outbounds are different, the rule or DNS setup is not complete yet.

{{% details title="Advanced checks" closed="true" %}}
If you want deeper diagnostics, check the routing health endpoint:

```bash {filename="bash"}
curl http://127.0.0.1:8080/api/health/routing
```

Look for entries with `"status": "missing"` or `"status": "mismatch"`.

Only if you are intentionally using custom low-level routing settings, also verify that `fwmark.start`, `fwmark.mask`, and `iproute.table_start` do not conflict with existing rules:

```bash {filename="bash"}
ip rule list
ip route show table 150
```
{{% /details %}}

## DNS rules do not work

1. Make sure the list name in `dns.rules` matches the list name in `lists`.
2. Make sure the DNS rule points to the correct DNS server tag.
3. If the DNS server uses `detour`, make sure that outbound is up.
4. Restart keen-pbr after editing the DNS section.

{{% details title="Advanced checks" closed="true" %}}
For manual or headless dnsmasq integration:

1. Verify `generate-resolver-config` produces output:

```bash {filename="bash"}
keen-pbr generate-resolver-config dnsmasq-nftset
```

2. Ensure dnsmasq includes a matching `conf-script=` line.
3. Restart dnsmasq after changing that line.
{{% /details %}}

## Remote lists do not update

1. Run:

```bash {filename="bash"}
keen-pbr download
```

2. If the list still does not update, check whether the URL is reachable from the router.
3. If you use automatic refresh, check that `lists_autoupdate.cron` is set to the schedule you want.

{{% details title="Advanced checks" closed="true" %}}
If you need to force a full reload:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

Also confirm that `daemon.cache_dir` is writable.
{{% /details %}}

## `urltest` always shows degraded

1. Make sure the test `url` is reachable.
2. Make sure the child outbounds are up.
3. Wait for the next probe cycle, or lower `interval_ms` temporarily while testing.

{{% details title="Advanced checks" closed="true" %}}
Check `GET /api/health/service` for circuit breaker state. If a child is `"open"`, wait for `circuit_breaker.timeout_ms` to expire.
{{% /details %}}

## Port/address filter rules not matching

{{< callout type="warning" >}}
Mixed negation in `src_addr` / `dest_addr` is not supported. All entries in the array must either all start with `!` or none of them should.
{{< /callout >}}

If rules aren't matching as expected:
- Verify that `proto` is set correctly (`"tcp"`, `"udp"`, or `"tcp/udp"`)
- Ensure negated address arrays use `!` on **every** entry, not just some
- Check that the list name in the rule matches exactly (case-sensitive) the key in `lists`

## Low-level routing conflicts (advanced)

If you changed `fwmark` or `iproute` settings, or another tool is also managing advanced routing on the same system, packets may be misrouted or dropped.

Check for conflicts:

```bash {filename="bash"}
ip rule list
nft list ruleset | grep mark
```

Adjust to a non-conflicting range:

```json
{
  "fwmark": {
    "start": "0x00020000",
    "mask": "0x00FF0000"
  }
}
```

The `mask` must be exactly two adjacent hex nibbles. Use hex strings such as `"0x00FF0000"` and `"0x00020000"`.
