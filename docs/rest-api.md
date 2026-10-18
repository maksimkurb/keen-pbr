# REST API

The REST API lets scripts and external systems control and monitor keen-pbr. For example:

- Prometheus and Grafana show VPN channel health and latency (see [GET /metrics](#get-metrics)).
- A cron job refreshes lists on a schedule through `POST /api/lists/refresh`.
- A script restarts routing through `POST /api/service/restart`.

If you only use the web interface, you can skip this page.

The REST API is available when:
- Full package version is installed (`keen-pbr`, not `keen-pbr-headless`)
- The config has `api.enabled: true`
- The `--no-api` flag was not passed at startup

## Configuration

In the web interface, open **Security** > **Authentication and CORS**. Turn on **Enable authentication**, enter a **New password** and confirm it. If a website on another address must call the API from a browser, add its address to **Allowed CORS origins** (one per line). Changes take effect immediately.

Set the device name in **General** > **Settings** > **General** > **Device name**. It appears in the browser tab title and under the keen-pbr logo, which helps tell several routers apart.

The same settings in `config.json`:

```json { filename="config.json" }
{
  "device_name": "Home router",
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121",
    "authentication": {
      "enabled": true,
      "password_hash": "argon2id$v=19$m=2048,t=2,p=1$..."
    },
    "cors": {
      "allowed_origins": ["https://panel.example.com"]
    }
  }
}
```

`device_name` is optional and may be empty.

If you edit `config.json` by hand, generate the password hash with this command (it asks for the password):

```bash
keen-pbr hash-password
```

To write the hash straight into the file, run `keen-pbr --config /path/to/config.json hash-password --update`. Then restart the keen-pbr service: the daemon does not notice changes made to the file from outside.

The password is stored as an Argon2id hash. Hashes from older keen-pbr versions do not work: set the password again.

When authentication is on, requests to the API use one of two methods:

- Basic authentication: username `admin` and your password (`curl -u admin:password ...`).
- Bearer token: a temporary key issued by `POST /api/auth/login` and sent in the header `Authorization: Bearer <token>`. A token is valid for 24 hours. Only one token is valid at a time: a new login, including one in the web interface, cancels the previous one. After the daemon restarts, get a new token.

Without logging in, only `GET /api/auth/status` and `POST /api/auth/login` are available. `/metrics` is protected like the rest of the API.

When authentication is off, requests from other websites in a browser are blocked. When it is on, addresses from **Allowed CORS origins** are allowed, as well as Chrome and Firefox extensions. The password travels over the network in clear text, so use a trusted network or HTTPS through a reverse proxy (a web server in front of keen-pbr).

By default, the API listens on `0.0.0.0:12121` (`api.listen`). All routes are served at that address.

The [OpenAPI document](https://github.com/maksimkurb/keen-pbr/blob/main/docs/openapi.yaml) is the complete request and response schema. The main routes are listed below (an outbound is a channel that traffic goes through: a VPN interface, your ISP, or a block):

| Route | Purpose |
|---|---|
| `GET /api/health/service` | Service, config draft, current start/stop operation, and rollback status. |
| `POST /api/service/start`, `/stop`, `/restart` | Start, stop, or restart routing asynchronously. |
| `GET /api/config`, `POST /api/config` | Read or stage a validated config draft. |
| `GET /api/config/defaults` | Default values for settings that are not set in the config. |
| `POST /api/config/discard`, `/save`, `/rollback` | Discard a draft, save and apply it, or return to the previous config after a failed apply. |
| `GET /api/status/events` | Server-Sent Events (SSE) stream: the server pushes changes, so you do not need to poll. Events: `snapshot` (full state on connect), `service`, `outbounds`, `interfaces`, `issues`, `routing`, `auth`, `config`. A keep-alive signal arrives every 15 seconds. The `config` event carries only a revision number; read the data with `GET /api/config`. After a dropped connection, the new connection starts with a fresh `snapshot`. |
| `GET /api/health/issues` | Detected problems and kernel check results: the same list as the overview page and `keen-pbr status`. |
| `GET /api/runtime/outbounds`, `/api/runtime/interfaces` | Current outbound state and the list of system interfaces. |
| `GET /api/health/routing`, `POST /api/routing/test` | Routing and firewall diagnostics, and a route check for a specific domain or IP address. |
| `POST /api/lists/refresh` | Refresh one or all remote URL lists. |
| `POST /api/dns/servers/check` | Checks the DNS servers of the applied configuration: whether they work and whether they tamper with answers. |
| `GET /api/dns/test` | Stream of DNS and TLS/QUIC interception events, or the DNS check in the web interface. |
| `GET /api/diagnostics/command-failure` | Log of recent system command failures. |
| `GET /metrics` | Prometheus text metrics. |
| `/api/auth/status`, `/api/auth/login`, `/api/auth/logout`, `/api/auth/password`, `/api/auth/settings` | Authentication status, login and logout, password (`GET` shows whether one is set; `POST` sets a new one), and authentication and CORS settings. |

---

## GET /api/health/service

Returns the running daemon version and routing runtime status.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Response

```json
{
  "version": "3.4.0",
  "build": "20261010120000",
  "status": "running",
  "runtime_state": "running",
  "runtime_state_reason": "config apply complete",
  "os_type": "debian",
  "os_version": "12",
  "build_variant": "generic",
  "doh_supported": true,
  "config_is_draft": false,
  "rollback_available": false
}
```

`status` is `running`, `degraded` (DNS interception is enabled but unavailable) or `stopped`. `doh_supported` tells whether this build can use DNS servers of type `doh`.

When interception is enabled, the response includes an `intercept` object: whether DNS interception (`dns_hold_active`) and traffic analysis (`l7_active`) are active, the kernel features available (`nfqueue`, `nflog`, `connbytes`, `addrtype`), and counters of processed packets, errors and queue overruns ({{< term "NFQUEUE" >}}/NFLOG). These values are mostly useful for diagnostics.

For channel state (health, latency) see `GET /api/runtime/outbounds`.

## GET /metrics

Returns metrics in Prometheus text format 0.0.4. Prometheus requests this address periodically (a scrape) and stores the values. Authentication is the same as for the API: use Basic authentication or the current Bearer token. If metrics are unavailable, the response is `503`:

```bash
curl -u admin:password http://127.0.0.1:12121/metrics
```

All durations are in **seconds** (fractional numbers). If a value is unknown, the metric is **omitted** instead of being exported as `0` (see "Omitted series" below).

### Process and configuration

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `keen_pbr_build_info` | gauge | `version`, `commit`, `firewall_backend` | Always `1`; build identity. |
| `keen_pbr_process_start_time_seconds` | gauge | - | Unix start time; uptime is `time() - keen_pbr_process_start_time_seconds`. |
| `keen_pbr_active_rules` | gauge | - | Number of applied firewall rules. |
| `keen_pbr_config_reload_last_success_timestamp_seconds` | gauge | - | Unix time of the last completed config apply (startup counts). Omitted until the first one. |
| `keen_pbr_config_reload_errors_total` | counter | - | Failed config reloads and applies (parse, validation, prepare or apply failures). |
| `keen_pbr_list_last_update_timestamp_seconds` | gauge | `list` | Unix time of the last successful check of a remote (URL) list, whether or not its content changed. Omitted until the first successful refresh after startup. |
| `keen_pbr_list_update_errors_total` | counter | `list` | Failed refresh attempts of a remote list. |

### Errors

`keen_pbr_errors_total{subsystem}` counts errors by subsystem. All values are always present, zeros included: `firewall_apply` (applying firewall rules), `netlink` (setting up routes), `set_write` (writing addresses to sets), `conntrack`, `dns_parse` (parsing DNS packets), `dns_tcp_partial` (incomplete DNS-over-TCP messages) and `dns_late_write` (failed deferred writes after a DNS answer).

### Probes (urltest / icmptest)

A probe is a periodic check of a channel: a request to a URL (`urltest`) or a ping (`icmptest`). Labels: `outbound` (the channel being checked), `test_outbound` (the probe group), `interface`, `type` (`urltest` or `icmptest`).

| Metric | Type | Meaning |
|---|---|---|
| `keen_pbr_probe_attempts_total` | counter | Accepted probe results. |
| `keen_pbr_probe_successes_total` | counter | Successful probe results. |
| `keen_pbr_probe_packets_sent_total` | counter | {{< term "ICMP" >}} echo requests sent. `type="icmptest"` only. |
| `keen_pbr_probe_packets_received_total` | counter | ICMP echo replies received. `type="icmptest"` only. |
| `keen_pbr_probe_up` | gauge | `1` if the last probe succeeded, `0` if it failed. |
| `keen_pbr_probe_last_success_timestamp_seconds` | gauge | Unix time of the last successful probe. |
| `keen_pbr_probe_latency_seconds` | gauge | Latency of the last successful probe: ICMP is the mean of the received replies, URL is the request time. Sub-millisecond precision. |
| `keen_pbr_probe_latency_min_seconds`, `keen_pbr_probe_latency_max_seconds` | gauge | Fastest and slowest reply of the last successful ICMP probe. `type="icmptest"` only. |
| `keen_pbr_urltest_selected` | gauge | Labels `group`, `outbound`: `1` for the group's currently selected outbound, `0` for the other members. |
| `keen_pbr_urltest_selection_changes_total` | counter | Label `group`: selection changes since the config was last applied (the counter resets on apply). |

#### Omitted series

- `probe_up` is omitted before the first probe of an outbound completed.
- `probe_latency_seconds`, `probe_latency_min_seconds` and
  `probe_latency_max_seconds` are omitted when the **last** probe failed or no
  probe completed yet. In Grafana this shows up as a gap, never as a `0`.
- `probe_last_success_timestamp_seconds` is omitted until the first success.
- `list_last_update_timestamp_seconds` and
  `config_reload_last_success_timestamp_seconds` are omitted until the first success.

### Interception

Metrics for DNS interception and traffic (L7) analysis. `path` is `dns` or `l7`.

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `keen_pbr_intercept_packets_total` | counter | `path` = `dns`, `l7` | Packets inspected. |
| `keen_pbr_intercept_matches_total` | counter | `path` = `dns`, `l7` | Packets that matched a configured domain list. |
| `keen_pbr_dns_hold_duration_seconds` | histogram | - | Time a DNS answer was held before its verdict. |
| `keen_pbr_dns_queue_wait_duration_seconds` | histogram | - | Time a DNS packet waited in the receive queue. |
| `keen_pbr_dns_hold_timeouts_total` | counter | `cause` = `batch_budget`, `admission_blocked`, `own_write_slow`, `late_batch_full`, `other` | Holds that reached their deadline. |
| `keen_pbr_dns_late_writes_total` | counter | - | DNS writes completed after the packet was released. |
| `keen_pbr_queue_overruns_total` | counter | `queue` = `nfqueue`, `nflog` | Kernel queue receive overruns. |
| `keen_pbr_set_write_duration_seconds` | histogram | `path` = `dns`, `late_dns`, `l7` | Dynamic set write time. Buckets (seconds): 0.0001, 0.00025, 0.0005, 0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, +Inf. |
| `keen_pbr_set_writes_total` | counter | `kind` = `add`, `refresh` | Elements added (upserted) or timeout-refreshed. |
| `keen_pbr_set_refresh_total` | counter | `result` = `skipped`, `deferred`, `dropped` | Cached-element refresh outcomes. `dropped` means the refresh queue was full. |
| `keen_pbr_set_cache_lookups_total` | counter | `result` = `hit`, `miss` | Set cache lookups; a hit avoids a write before the verdict. |
| `keen_pbr_set_cache_entries` | gauge | - | Remembered dynamic set elements. |
| `keen_pbr_conntrack_requests_total` | counter | - | Conntrack cleanup requests. |
| `keen_pbr_conntrack_deleted_total` | counter | - | Conntrack entries deleted. |

### Balancing and marked packets

{{< only iptables >}} These metrics exist only with this firewall backend. A series with no data is omitted. Counters restart from zero when the firewall rules are rebuilt.

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `keen_pbr_balance_classifications_total` | counter | `outbound`, `candidate`, `family` = `ipv4`, `ipv6` | New connections that a balance group sent through the `candidate` channel. |
| `keen_pbr_skip_marked_packets_total` | counter | `family` = `ipv4`, `ipv6` | Packets keen-pbr skipped because they were already marked (**General** > **Settings** > **General** > **Skip packets that are already marked**). |

The "slow write" share is derived from the `le="0.025"` bucket of
`keen_pbr_set_write_duration_seconds` (writes slower than 25 ms).

Prometheus scrape configuration:

```yaml
scrape_configs:
  - job_name: keen-pbr
    metrics_path: /metrics
    scrape_interval: 15s
    static_configs:
      - targets: ["router.example:12121"]
    basic_auth:
      username: admin
      password_file: /etc/prometheus/keen-pbr-password
```

Example queries:

```promql
# ICMP packet loss over 15 minutes, in percent (no data when nothing was sent)
100 * clamp(1 - increase(keen_pbr_probe_packets_received_total[15m])
    / (increase(keen_pbr_probe_packets_sent_total[15m]) > 0), 0, 1)

# Probe success rate over 15 minutes
100 * increase(keen_pbr_probe_successes_total[15m])
    / (increase(keen_pbr_probe_attempts_total[15m]) > 0)

# Average and 95th percentile latency per outbound over 1 hour (seconds)
avg_over_time(keen_pbr_probe_latency_seconds[1h])
quantile_over_time(0.95, keen_pbr_probe_latency_seconds[1h])

# DNS hold time p99 (seconds)
histogram_quantile(0.99, sum by (le)
    (rate(keen_pbr_dns_hold_duration_seconds_bucket[5m])))

# Share of set writes slower than 25 ms, per path
1 - sum by (path) (rate(keen_pbr_set_write_duration_seconds_bucket{le="0.025"}[5m]))
  / (sum by (path) (rate(keen_pbr_set_write_duration_seconds_count[5m])) > 0)

# Errors in the last hour by subsystem
sum by (subsystem) (increase(keen_pbr_errors_total[1h])) > 0
```

### DNS proxy {#dns-proxy}

Metrics of the built-in DNS proxy, where dnsmasq sends queries for domains of lists with several servers or with DNS-over-HTTPS. When the proxy is not used, these metrics are not published. Labels:

- `proxy_port`: the proxy port, one per set of servers;
- `server`: the server names (`tag`), comma-separated;
- `upstream`: the server address (`ip:port`) or the DNS-over-HTTPS URL;
- `type`: `dns` or `doh`;
- `detour`: the outbound of the server, empty when not set.

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `keen_pbr_dns_proxy_queries_total` | counter | `proxy_port`, `transport` = `udp`, `tcp` | Queries received from dnsmasq, including dropped ones. |
| `keen_pbr_dns_proxy_dropped_total` | counter | `proxy_port`, `transport`, `reason` = `overload`, `malformed` | Queries the proxy did not serve: no free room (`overload`) or not a DNS query (`malformed`). |
| `keen_pbr_dns_proxy_responses_total` | counter | `proxy_port`, `source` = `upstream`, `proxy_servfail` | Answers to clients: from a server (`upstream`) or the proxy's own SERVFAIL when no server answered (`proxy_servfail`). |
| `keen_pbr_dns_proxy_upstream_requests_total` | counter | `proxy_port`, `server`, `upstream`, `type`, `detour`, `result` | Attempts to get an answer from a server, by result: `ok`, `nxdomain`, `servfail`, `refused`, `other_rcode`, `timeout`, `connection_refused`, `unreachable`, `connection_closed`, `tls_error`, `http_error`, `bad_answer`, `not_resolved`, `network_error`. |
| `keen_pbr_dns_proxy_upstream_response_duration_seconds` | histogram | as above, without `result` | Time until the server answered. Only attempts that got an answer are counted. Bounds (s): 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, +Inf. |
| `keen_pbr_dns_proxy_upstream_up` | gauge | as above, without `result` | `0` while the proxy skips a server after an error (or cannot use it), `1` otherwise. |
| `keen_pbr_dns_proxy_upstream_consecutive_failures` | gauge | as above, without `result` | Attempts in a row with no answer, or with SERVFAIL or REFUSED. Any other answer resets the counter. At 10 the warning "DNS server is not answering" appears. |
| `keen_pbr_dns_proxy_inflight` | gauge | `transport` | Queries the proxy is working on now. |

Share of failed attempts per server over 5 minutes:

```promql
sum by (server) (rate(keen_pbr_dns_proxy_upstream_requests_total{result=~"timeout|connection_refused|servfail|refused"}[5m]))
  / sum by (server) (rate(keen_pbr_dns_proxy_upstream_requests_total[5m]))
```

### Grafana dashboard and alerts

Import [keen-pbr-dashboard.json](https://keen-pbr.fyi/grafana/keen-pbr-dashboard.json) into Grafana (Dashboards > New > Import). It needs a Prometheus data source with UID `prometheus`. The dashboard has `job` and `outbound` variables, a 30 s refresh, and the sections Status, Outbounds, DNS interception, DNS proxy, Sets & lists and Errors. Use a scrape interval of 30 s or less: the bar panels compute `increase()` over one-minute windows.

Example alerting rules:

```yaml
groups:
  - name: keen-pbr
    rules:
      - alert: KeenPbrDown
        expr: up{job="keen-pbr"} == 0
        for: 2m
      - alert: KeenPbrOutboundDown
        expr: keen_pbr_probe_up == 0
        for: 5m
        annotations:
          summary: "{{ $labels.outbound }} probe is failing ({{ $labels.test_outbound }})"
      - alert: KeenPbrProbeStale
        # Probes stopped completing (probe interval must be well below 30m).
        expr: increase(keen_pbr_probe_attempts_total[30m]) == 0
        for: 10m
      - alert: KeenPbrErrors
        expr: sum by (subsystem) (increase(keen_pbr_errors_total[10m])) > 0
        for: 5m
      - alert: KeenPbrDnsHoldSlow
        # 0.03 s is the default hold deadline (intercept hold_timeout_ms: 30).
        expr: >
          histogram_quantile(0.99, sum by (le)
            (rate(keen_pbr_dns_hold_duration_seconds_bucket[5m]))) > 0.03
        for: 10m
      - alert: KeenPbrQueueOverruns
        expr: sum by (queue) (increase(keen_pbr_queue_overruns_total[10m])) > 0
      - alert: KeenPbrListStale
        expr: time() - keen_pbr_list_last_update_timestamp_seconds > 86400
        for: 30m
```

---

## POST /api/lists/refresh

Refreshes remote URL-backed lists from the active daemon config.

- If `name` is provided, only that URL-backed list is refreshed.
- If `name` is omitted, all URL-backed lists are refreshed.
- If list contents changed and that affects routing or DNS, keen-pbr applies the changes immediately.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/lists/refresh \
  -H "Content-Type: application/json" \
  -d '{"name":"apple"}'
```

Refresh all URL-backed lists:

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/lists/refresh
```

### Request Body (optional)

```json
{
  "name": "apple"
}
```

- `name` *(optional string)*: List name to refresh.

### Response (200)

```json
{
  "status": "ok",
  "message": "Lists refreshed and runtime reloaded",
  "refreshed_lists": ["apple", "google"],
  "changed_lists": ["apple"],
  "failed_lists": [],
  "reloaded": true
}
```

Success payload fields:

- `refreshed_lists` *(array[string])*: URL-backed lists that were refreshed.
- `changed_lists` *(array[string])*: Refreshed lists whose cached contents changed.
- `failed_lists` *(array[string])*: URL-backed lists that could not be refreshed.
- `reloaded` *(boolean)*: Whether the changes were applied because the changed lists are in use.

### Status / Error Behavior

- `200`: Refresh operation completed.
- `400`: Requested list exists but is not URL-backed.
- `404`: Requested list not found.
- `409`: Refresh rejected: there is an unsaved config draft or another operation is already running.

Error response body:

```json
{
  "error": "human-readable message"
}
```

---

## GET /api/config

Returns the current configuration and a flag showing whether an unsaved draft exists. The `api` settings (password, CORS) are not included. The response also contains list refresh state.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/config
```

### Response

```json
{
  "config": {
    "daemon": { "pid_file": "/var/run/keen-pbr.pid", "cache_dir": "/var/cache/keen-pbr" },
    "outbounds": [],
    "lists": {},
    "route": {}
  },
  "is_draft": false,
  "list_refresh_state": {}
}
```

`is_draft` is `true` when a config was submitted via `POST /api/config` but not yet saved to disk.

### Error Response (500)

```json
{
  "error": "Cannot open config file"
}
```

---

## POST /api/config

Validates the submitted JSON as a config file and keeps it in memory as a draft. Nothing is written to disk and routing does not change. To save and apply the draft, call `POST /api/config/save`.

The `api` field in the request is ignored.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config \
  -H "Content-Type: application/json" \
  -d @new-config.json
```

### Response

```json
{
  "status": "ok",
  "message": "Config staged in memory"
}
```

### Error Response (400 — validation error)

```json
{
  "error": "Validation failed",
  "validation_errors": [
    { "path": "outbounds[0].interface", "message": "interface is required" }
  ]
}
```

---

## POST /api/config/save

Starts saving the draft to disk and applying the configuration in the background. `202` means only that the operation was accepted, not that it has finished.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config/save
```

### Accepted response (202)

```json
{
  "operation_id": "op-123",
  "status": "accepted"
}
```

### Error Response (400 — no draft)

```json
{
  "error": "No staged config to save"
}
```

Other errors: `400` (the draft failed validation), `409` (another operation is already running), `500`.

After `202`, watch the `lifecycle_operation` field in `GET /api/health/service`, or subscribe to `/api/status/events`, until the operation finishes.

If applying failed after the file was saved, `GET /api/health/service` shows `rollback_available: true`. The file stays new. To return to the previous config, call `POST /api/config/rollback` (`409` if there is nothing to roll back or another operation is running). `POST /api/config/discard` only drops an unsaved draft; it changes neither the file nor routing.

---

## GET /api/runtime/outbounds

Returns the current state of outbounds: which channel a urltest or icmptest group has selected right now (the group picks the channel with the lowest latency on its own), whether an interface is reachable, and whether it was switched off after a series of failures.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/runtime/outbounds
```

### Response

```json
{
  "outbounds": [
    {
      "tag": "vpn",
      "type": "interface",
      "status": "healthy",
      "interfaces": [
        { "outbound_tag": "vpn", "interface_name": "tun0", "status": "active" }
      ]
    },
    {
      "tag": "auto_select",
      "type": "urltest",
      "status": "healthy",
      "interfaces": [
        { "outbound_tag": "vpn", "interface_name": "tun0", "status": "active" },
        { "outbound_tag": "vpn2", "interface_name": "tun1", "status": "backup" }
      ]
    }
  ]
}
```

- `type`: `interface`, `table`, `blackhole`, `ignore`, `urltest` or `icmptest`.
- Outbound `status`: `healthy` (working), `degraded` (partly working), `unavailable` (no usable path), `unknown`.
- `status` in `interfaces`: `active` (traffic goes through it), `backup` (standby), `degraded`, `unavailable` (switched off after a series of failures), `unknown`. In a urltest or icmptest group the selected channel is `active`.
- Optional fields: `latency_ms`, `probe_target`, `detail` and packet counters.

---

## POST /api/dns/servers/check

Asks all DNS servers of the applied configuration (not the draft) about a few sites and shows which servers work, do not answer or tamper with answers. A `keenetic` server is checked at each address Keenetic reports. Only one check runs at a time. All body fields are optional.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/dns/servers/check \
  -H "Content-Type: application/json" \
  -d '{"domains": ["yandex.ru", "youtube.com"], "custom_domains": ["example.org"], "servers": ["google_dns"]}'
```

- `domains`: sites that certainly exist; the answer "does not exist" for them is suspicious. Defaults to `["yandex.ru"]` unless `custom_domains` is set.
- `custom_domains`: your own sites; "does not exist" or no addresses is not suspicious.
- `servers`: server names (`tag`) from `dns.servers`. All are checked by default.

You can name at most 5 sites in total.

### Response

```json
{
  "overall": "warning",
  "servers": [
    {
      "tag": "google_dns",
      "type": "static",
      "endpoints": [
        {
          "address": "8.8.8.8",
          "status": "ok",
          "nxdomain_hijack": false,
          "results": [
            {
              "domain": "yandex.ru",
              "status": "ok",
              "severity": "ok",
              "rcode": 0,
              "addresses": ["77.88.55.60"],
              "duration_ms": 14.2
            },
            {
              "domain": "youtube.com",
              "status": "nxdomain",
              "severity": "warning",
              "rcode": 3,
              "addresses": [],
              "duration_ms": 9.8
            }
          ]
        }
      ]
    }
  ]
}
```

- `overall` is `ok`, `warning` (signs of tampering) or `error` (a server does not answer).
- A result's `status` is `ok`, `nxdomain`, `nodata`, `stub_address`, `servfail`, `refused`, `rcode`, or the reason there is no answer: `timeout`, `connection_refused`, `unreachable`, `connection_closed`, `tls_error`, `http_error`, `bad_answer`, `not_resolved`, `network_error`. `severity` is `ok`, `warning` or `error`.
- `nxdomain_hijack` is `true` when the server answered a name that does not exist with addresses.
- An address with status `skipped` was not checked (for example, IPv6 is off); `unavailable` means it cannot be checked (for example, DNS-over-HTTPS without certificates). The reason is in `reason`.

Errors: `400` for an invalid body, more than 5 sites, an invalid site name or an unknown `tag`; `409` while the previous check is still running; `503` while the service is stopped.

---

## POST /api/routing/test

Shows which outbound traffic to a domain or IP address will use. For a domain it first resolves the IP addresses. It then checks the routing rules against the lists (the expected outbound) and the firewall sets (the actual outbound). This helps find a mismatch without restarting the daemon.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/routing/test \
  -H "Content-Type: application/json" \
  -d '{"target": "example.com", "proto": "tcp", "dest_port": 443}'
```

The optional packet fields (`proto`: `tcp`, `udp`, or `other`; `dest_port`,
`src_addr`, `src_port`, and `dscp`) let you test rules that filter by port,
address, or {{< term "DSCP" >}}. Without them, only list membership of the target is checked.

### Response

```json
{
  "target": "example.com",
  "is_domain": true,
  "resolved_ips": ["93.184.216.34"],
  "results": [
    {
      "ip": "93.184.216.34",
      "expected_outbound": "vpn",
      "actual_outbound": "vpn",
      "ok": true,
      "list_match": { "list": "my_domains", "via": "domain" }
    }
  ],
  "warnings": [],
  "rule_diagnostics": [],
  "no_matching_rule": false
}
```

- `ok` means the expected and actual outbound match. Instead of an outbound name you may see `(default)` (no rule matched, traffic uses the default) or `(unknown)` (could not be determined).
- `warnings` holds non-fatal warnings, for example a DNS error. `rule_diagnostics` is a per-rule breakdown (shown in the web interface). `no_matching_rule` is `true` when no rule matched.

---

## GET /api/health/routing

Returns a report on whether routes and the firewall are set up correctly: firewall rules with packet marks (an fwmark is a number the kernel uses to pick a route), route tables, and the rules that select a table. The report is refreshed in the background, so right after startup it may briefly show an incomplete result.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
```

### Response

```json
{
  "overall": "ok",
  "firewall_backend": "iptables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true,
    "verification_state": "verified",
    "detail": "ok"
  },
  "firewall_rules": [
    {
      "set_name": "<direct>",
      "action": "mark",
      "expected_fwmark": "0x00010000",
      "actual_fwmark": "0x00010000",
      "status": "ok"
    }
  ],
  "route_tables": [
    {
      "table_id": 150,
      "outbound_tag": "vpn",
      "expected_interface": "tun0",
      "expected_gateway": "10.8.0.1",
      "table_exists": true,
      "default_route_present": true,
      "interface_matches": true,
      "gateway_matches": true,
      "status": "ok"
    }
  ],
  "policy_rules": [
    {
      "fwmark": "0x00010000",
      "fwmask": "0x00ff0000",
      "expected_table": 150,
      "priority": 1000,
      "rule_present_v4": true,
      "rule_present_v6": true,
      "status": "ok"
    }
  ]
}
```

`firewall.verification_state` is `verified`, `unavailable` (firewall data not available) or `failed`. `firewall.detail` is `ok` on success and describes the cause otherwise. The optional `warnings` array lists warnings (for example, NAT is not set up or strict `rp_filter` is on); they do not affect `overall`.

**Overall status values:**
- `ok` — all checks passed
- `degraded` — one or more checks failed
- `error` — an exception prevented checks from completing

**Check status values:**
- `ok` — check passed
- `missing` — expected element not found in kernel
- `mismatch` — element found but configuration differs

### Error Response (500)

```json
{
  "overall": "error",
  "error": "failed to connect to netlink socket"
}
```

---

## GET /api/dns/test

Streams interception events as Server-Sent Events (the server sends events as they happen). `HELLO` comes first. The DNS check in the web interface uses this stream: it requests a special marker domain (`check.keen-pbr.fyi`) and waits for it to appear in the stream.

- `show=keen-pbr` (default): only marker events. The stream closes when the matching marker arrives. Pass `domain=<marker>` to keep your check separate from others.
- `show=all` (or `show=full`): the stream stays open and shows DNS queries, TLS {{< term "SNI" >}}, HTTP Host, {{< term "QUIC" >}} Initial and markers, including domains that are not in any list.

`show=all` exposes the traffic of every client, so it works only when API authentication is on. Without it the response is `403`. The default view is always available. An invalid `show` or `domain` returns `400`.

```bash {filename="bash"}
curl -N -u admin:password 'http://127.0.0.1:12121/api/dns/test?show=all'
```

### Stream Example

```text
data: {"type":"HELLO"}

data: {"type":"INTERCEPT","seq":42,"ts_ms":1712345678123,"source":"dns","client_ip":"192.168.1.10","domain":"example.com","lists":["streaming"],"ips":["203.0.113.7"],"added":1,"refreshed":0,"errors":0,"hold_us":180,"timed_out":false}

```

