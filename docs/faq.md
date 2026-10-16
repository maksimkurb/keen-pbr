# FAQ

## What firewall backend will be used?

It depends on `daemon.firewall_backend`. The default is `"auto"`: keen-pbr uses nftables if the `nft` command is available, and iptables/ipset otherwise. On Keenetic, iptables/ipset is always used.

## How do I reload lists without restarting the service?

Run the command for your platform:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr reload
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service keen-pbr reload
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl reload keen-pbr
```
{{< /tab >}}
{{< /tabs >}}

This is a full reload: it re-downloads all remote lists and re-applies the firewall and routing rules.

In the `keen-pbr-headless` build this command does not work. To re-download the lists and apply them, run `keen-pbr download --reload`.

{{% details title="What `reapply-firewall` does" closed="true" %}}
On Keenetic there is also `/opt/etc/init.d/S80keen-pbr reapply-firewall`. It re-verifies the routing tables and starts the `urltest` channel checks immediately, but does not re-download lists.
{{% /details %}}

## What happens if a remote list URL is unreachable at startup?

keen-pbr uses the cached copy from `daemon.cache_dir` if available. If there is no cache either, a list that has only `url` stays empty until a download succeeds; keen-pbr keeps retrying in the background.

## How does the urltest outbound select the best link?

Every `interval_ms`, keen-pbr sends an HTTP request to the configured `url` through each link and measures the round-trip latency. With `strategy: "priority"`, the link with the lowest latency is selected. If the new best link is faster than the current one by less than `tolerance_ms`, keen-pbr does not switch. Links are checked group by group: the next group in `outbound_groups` is used only when no link in the previous one works.

If a link fails `failure_threshold` checks in a row, keen-pbr stops using it for `timeout_ms`, then tries it again with a few checks. This keeps traffic from bouncing between links while one of them is unstable.

## Can I route DNS queries through a VPN?

Yes. Use the `detour` field in `dns.servers` to bind DNS queries for that server to a specific outbound:

```json
{
  "servers": [
    {
      "tag": "vpn_dns",
      "address": "10.8.0.1",
      "detour": "vpn"
    }
  ]
}
```

## What's the list file format?

One entry per line. Supported formats:
- IPv4 address: `93.184.216.34`
- IPv6 address: `2606:2800:220:1:248:1893:25c8:1946`
- {{< term "CIDR" >}}: `10.0.0.0/8`
- Domain: `example.com` (automatically includes all sub-domains)
  - entries like `*.example.com` work the same way as `example.com`
- Lines starting with `#` are comments and are skipped
- Empty lines are skipped

## How do I verify routing is correctly applied?

Run:

```bash {filename="bash"}
keen-pbr status
```

keen-pbr compares the firewall rules, routing tables and `ip rule` entries in the kernel with what it expects and marks each item `OK`, `MISSING`, `MISMATCH` or `ERROR`. The same check is in the web UI: the **Diagnostics** panel on the **General** > **Dashboard** page. To check a specific site, click **Where does this traffic go?** on the same page or run `keen-pbr test-routing <domain-or-ip>`.

## Why does a check from the router itself not go through the VPN? {#router-own-traffic}

The **Process router's own traffic** (shown in **Advanced** settings mode) option (**General** > **Settings** > **General**) is off by default: the rules apply to traffic from devices on your local network, not to commands like `curl` in the router's SSH console. Test routing from a device on the network. Also, if the **Processed (inbound) interfaces** list is not empty, make sure the device connects through one of those interfaces.

## Can I combine multiple sources in one list?

Yes. `url`, `domains`, `ip_cidrs`, and `file` can all be set in the same list entry and the data is combined:

```json
{
  "lists": {
    "combined": {
      "url": "https://example.com/remote.txt",
      "domains": ["extra.example.com"],
      "ip_cidrs": ["192.168.100.0/24"],
      "file": "./local.txt"
    }
  }
}
```

{{< callout type="warning" >}}
The web UI can combine only `ip_cidrs` and `domains`. Do not combine them with `url` or `file`: the web UI does not support this, and a future version may reject it.
{{< /callout >}}

