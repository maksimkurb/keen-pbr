# Troubleshooting

Start with the symptom that matches what you see. Each section begins with the simplest checks first, then gives more advanced checks if you still need them.

## Service does not start

Follow the checks for your platform:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
1. Confirm that the config file exists: `/opt/etc/keen-pbr/config.json`
2. Restart the `keen-pbr` service:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr restart
   ```
3. Check whether `keen-pbr` is alive or dead:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr status
   ```
4. Check whether `dnsmasq` is alive or dead:
   ```bash {filename="bash"}
   /opt/etc/init.d/S56dnsmasq status
   ```
5. Read the router logs and search for `keen-pbr` or `dnsmasq`:
   ```bash {filename="bash"}
   ndmc -c "show log once" | grep -E 'keen-pbr|dnsmasq'
   ```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
1. Confirm that the config file exists: `/etc/keen-pbr/config.json`
2. Restart the `keen-pbr` service:
   ```bash {filename="bash"}
   service keen-pbr restart
   ```
3. Check whether `keen-pbr` is alive or dead:
   ```bash {filename="bash"}
   service keen-pbr status
   ```
4. Check whether `dnsmasq` is alive or dead:
   ```bash {filename="bash"}
   service dnsmasq status
   ```
5. Read the router logs and search for `keen-pbr` or `dnsmasq`:
   ```bash {filename="bash"}
   logread | grep -E 'keen-pbr|dnsmasq'
   ```
{{< /tab >}}
{{< tab name="Debian" >}}
1. Confirm that the config file exists: `/etc/keen-pbr/config.json`
2. Restart the `keen-pbr` service:
   ```bash {filename="bash"}
   service keen-pbr restart
   ```
3. Check whether `keen-pbr` is alive or dead:
   ```bash {filename="bash"}
   service keen-pbr status
   ```
4. Check whether `dnsmasq` is alive or dead:
   ```bash {filename="bash"}
   service dnsmasq status
   ```
5. Read the logs and search for `keen-pbr` or `dnsmasq`:
   ```bash {filename="bash"}
   journalctl -u keen-pbr -u dnsmasq
   ```
{{< /tab >}}
{{< /tabs >}}

#### Common tips
- If you recently edited the config file, check it for missing commas, broken JSON, or wrong paths.
- If `keen-pbr` service is alive but you can't open WebUI, make sure you did not accidentally replace the config with a headless-only example (`api` section must exist in the `config.json`).

{{% details title="Advanced checks" closed="true" %}}
Use these if the service still will not start:

1. Validate the JSON in your platform's config file:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
jq . /opt/etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< /tabs >}}

2. Make sure the directory for `daemon.pid_file` exists and is writable.
3. Make sure `daemon.cache_dir` exists and is writable.
4. If the API is enabled, make sure the configured listen address and port are not already in use.
5. If `keen-pbr` or `dnsmasq` shows as `dead`, return to the logs and look for the first startup error before restarting again.
{{% /details %}}

## Sites are not going through the VPN

1. Make sure the user device is using the router's DNS.
    - Open `http://<router-ip>:12121/` and look at DNS Check widget. It should say "DNS request from the browser reached dnsmasq".
    - Alternatively, run this command from your PC: `nslookup check.keen.pbr`. It should return `127.0.0.88`.
2. Run a routing test:
    - Open `http://<router-ip>:12121/` and enter `google.com` (or your IP/domain) into "Where does this traffic go?" widget.
    - Alternatively, run this command from your router: `keen-pbr test-routing google.com`
3. Make sure the domain/IP is in the correct list.
4. Make sure the route rule for that list points to your VPN outbound.
5. Make sure your VPN connection is actually up.

If the expected and actual outbounds are different, the rule or DNS setup is not complete yet.

{{% details title="Advanced checks" closed="true" %}}
If you want deeper diagnostics, check the routing health:

```bash {filename="bash"}
keen-pbr status
```

Look for entries with `"status": "missing"` or `"status": "mismatch"`.

Only if you are intentionally using custom low-level routing settings, also verify that `fwmark.start`, `fwmark.mask`, and `iproute.table_start` do not conflict with existing rules:

```bash {filename="bash"}
ip rule list
ip route show table <table_number>
```
{{% /details %}}

## DNS rules do not work

1. Make sure the list name in `dns.rules` matches the list name in `lists`.
2. Make sure the DNS rule points to the correct DNS server tag.
3. If the DNS server uses `detour`, make sure that outbound is up.
4. Restart keen-pbr after editing the DNS section.

{{% details title="Advanced checks" closed="true" %}}
1. Verify `generate-resolver-config` produces valid output:
    ```bash {filename="bash"}
    keen-pbr generate-resolver-config dnsmasq-nftset
    ```
2. Ensure dnsmasq config includes a matching `conf-file=`/`conf-script=` line.
3. Restart dnsmasq after changing that line.
{{% /details %}}

## Websites are not opening: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

1. Make sure `dns.fallback` is configured and points to at least one working DNS server tag.
2. Make sure the fallback DNS server itself is reachable from the router. If that DNS server uses `detour`, make sure the selected outbound is up.
3. Make sure the user device is using the router's DNS.
4. Restart keen-pbr after changing the DNS configuration.

Example:

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "default_dns",
        "address": "1.1.1.1"
      }
    ],
    "fallback": ["default_dns"]
  }
}
```

Without `dns.fallback`, domains that do not match any `dns.rules` entry may fail to resolve.

## Websites are not opening: `DNS_PROBE_FINISHED_BAD_CONFIG`

This usually means `dnsmasq` is not running or failed to apply its configuration.

1. Check the router logs for `dnsmasq` errors.
2. Make sure `dnsmasq` is running.
3. If you changed DNS settings recently, restart `keen-pbr` and `dnsmasq`.

Useful commands:

```bash {filename="bash"}
# for OpenWRT:
logread | grep dnsmasq

# for Keenetic/ Netcraze
ndmc -c "show log once" | grep dnsmasq

# for Debian
journalctl -u dnsmasq
```

## Remote lists do not update

1. Run on your router: `keen-pbr download`
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

1. Make sure the test `url` is reachable and returns good HTTP response (e.g. `200 OK` or `204 No content`).
2. Make sure the child outbounds are up.
3. Wait for the next probe cycle, or lower `interval_ms` temporarily while testing.

{{% details title="Advanced checks" closed="true" %}}
Check `GET /api/health/service` for circuit breaker state. If a child is `"open"`, wait for `circuit_breaker.timeout_ms` to expire.
{{% /details %}}

## Port/address filter rules not matching

{{< callout type="warning" >}}
When using negation in `src_addr` / `dest_addr` fields, the negation applies to all IP addresses/subnets specified in that field. Mixing entries with negation and without negation in the same list is not supported. As an alternative, you can create two separate rules.

This is also valid for `src_port` / `dest_port`.
{{< /callout >}}

If rules aren't matching as expected:
- Verify that `proto` is set correctly (`null` (for any), `"tcp"`, `"udp"`, or `"tcp/udp"`)
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

