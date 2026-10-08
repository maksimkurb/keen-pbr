---
title: REST API
weight: 5
aliases:
  - /docs/advanced/rest-api/
---

This page documents the built-in HTTP REST API.

The REST API is available when:
- Full package version is installed (`keen-pbr`, not `keen-pbr-headless`)
- The config has `api.enabled: true`
- The `--no-api` flag was not passed at startup

## Configuration

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

Generate a verifier interactively:

```bash
keen-pbr hash-password
```

To enable authentication and write the generated verifier directly to the
configured `config.json`, use `keen-pbr --config /path/to/config.json hash-password --update`.
The command updates the file atomically and reminds you to restart the keen-pbr
service because it edits the file outside the running daemon. Authentication
and CORS changes made through the WebUI are persisted and take effect
immediately, without applying a pending routing configuration draft.

`device_name` is optional and may be empty. When set, the WebUI uses it in the
browser page title and beneath the keen-pbr logo to distinguish devices.

Password verifiers use Monocypher's Argon2id implementation. Verifiers from
older PBKDF2-based releases are not accepted and must be regenerated.
The config API never returns or accepts the stored verifier. The WebUI reads
only `GET /api/auth/password` state and sends a new clear-text password only to
the write-only `POST /api/auth/settings` operation; hashing happens in the daemon.

Authenticated API clients may use Basic authentication with username `admin`
or a Bearer token returned by `POST /api/auth/login`. Only one Bearer session is
valid at a time; a successful login immediately invalidates the previous UI
session. Bearer sessions expire after 24 hours and are lost on daemon restart.

When authentication is disabled, cross-origin browser requests are rejected.
When enabled, exact configured origins and Chrome/Firefox extension origins are
allowed. Since passwords are sent to the API during login or Basic auth, use a
trusted network or an HTTPS reverse proxy.

By default, the API listens on `0.0.0.0:12121`. All endpoints are served at the configured `api.listen` address.

---

## GET /api/health/service

Returns the running daemon version and routing runtime status.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Response

```json
{
  "version": "3.0.0",
  "status": "running",
  "config_is_draft": false
}
```

When interception is configured, the same response includes an `intercept`
object. It reports `dns_hold_active` and `l7_active` independently, capability
flags (`nfqueue`, `nflog`, `connbytes`), and counters for DNS parsing/holds,
partial TCP, L7 packets, set updates, conntrack cleanup, and NFQUEUE/NFLOG
overruns. A missing NFQUEUE capability does not imply that L7 is unavailable,
and vice versa.

For live outbound runtime state (health, latency, circuit breaker) use `GET /api/runtime/outbounds`.

## GET /metrics

Returns Prometheus text format 0.0.4. It uses the same authentication as the API,
including when authentication is enabled; scrape it with Basic auth or the
current Bearer token:

```bash
curl -u admin:password http://127.0.0.1:12121/metrics
```

All durations are floating-point **seconds**. A series that is not meaningful is
**omitted** instead of being exported as `0` (see "Omitted series" below).
Every family has `# HELP` and `# TYPE` exactly once; families without samples
are not printed. This is a breaking change from earlier builds: the old
`keen_pbr_intercept_*`, `keen_pbr_dns_write_duration_seconds`, netlink timing,
probe histogram and success-ratio metrics no longer exist.

### Process and configuration

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `keen_pbr_build_info` | gauge | `version`, `commit`, `firewall_backend` | Always `1`; build identity. |
| `keen_pbr_process_start_time_seconds` | gauge | - | Unix start time; uptime is `time() - keen_pbr_process_start_time_seconds`. |
| `keen_pbr_active_rules` | gauge | - | Realized firewall rules. |
| `keen_pbr_config_reload_last_success_timestamp_seconds` | gauge | - | Unix time the runtime last finished applying a configuration (startup counts). Omitted until the first one. |
| `keen_pbr_config_reload_errors_total` | counter | - | Failed config reloads and applies (parse, validation, prepare or apply failures). |
| `keen_pbr_list_last_update_timestamp_seconds` | gauge | `list` | Unix time of the last successful check of a remote (URL) list, whether or not its content changed. In-memory: omitted until a refresh succeeded in this process. |
| `keen_pbr_list_update_errors_total` | counter | `list` | Failed refresh attempts of a remote list. |

### Errors

`keen_pbr_errors_total{subsystem}` is a single counter family; all values are
always present (zeros included): `firewall_apply` (failed firewall applies),
`netlink` (route / policy-rule operations), `set_write` (dynamic set writes),
`conntrack` (cleanup errors), `dns_parse`, `dns_tcp_partial` (DNS-over-TCP
messages not fully reassembled) and `dns_late_write` (failed deferred DNS
writes).

### Probes (urltest / icmptest)

Labelled with the child `outbound`, test group `test_outbound`, `interface` and
`type` (`urltest` or `icmptest`).

| Metric | Type | Meaning |
|---|---|---|
| `keen_pbr_probe_attempts_total` | counter | Accepted probe results. |
| `keen_pbr_probe_successes_total` | counter | Successful probe results. |
| `keen_pbr_probe_packets_sent_total` | counter | ICMP echo requests sent. `type="icmptest"` only. |
| `keen_pbr_probe_packets_received_total` | counter | ICMP echo replies received. `type="icmptest"` only. |
| `keen_pbr_probe_up` | gauge | `1` if the last probe succeeded, `0` if it failed. |
| `keen_pbr_probe_last_success_timestamp_seconds` | gauge | Unix time of the last successful probe. |
| `keen_pbr_probe_latency_seconds` | gauge | Latency of the last successful probe: ICMP is the mean of the received replies, URL is the request time. Sub-millisecond precision. |
| `keen_pbr_probe_latency_min_seconds`, `keen_pbr_probe_latency_max_seconds` | gauge | Fastest and slowest reply of the last successful ICMP probe. `type="icmptest"` only. |
| `keen_pbr_urltest_selected` | gauge | Labels `group`, `outbound`: `1` for the group's currently selected outbound, `0` for the other members. |
| `keen_pbr_urltest_selection_changes_total` | counter | Label `group`: selection changes since the group was registered (a config apply registers it again, which resets the counter). |

#### Omitted series

- `probe_up` is omitted before the first probe of an outbound completed.
- `probe_latency_seconds`, `probe_latency_min_seconds` and
  `probe_latency_max_seconds` are omitted when the **last** probe failed or no
  probe completed yet. In Grafana this shows up as a gap, never as a `0`.
- `probe_last_success_timestamp_seconds` is omitted until the first success.
- `list_last_update_timestamp_seconds` and
  `config_reload_last_success_timestamp_seconds` are omitted until the first success.

### Interception

All of these are updated on the DNS / L7 packet paths with a single relaxed
atomic increment on a pre-allocated counter; labels are fixed array slots that
are only rendered when `/metrics` is scraped.

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

The "slow write" share is derived from the `le="0.025"` bucket of
`keen_pbr_set_write_duration_seconds` (writes slower than 25 ms).

Not exported: per-outbound routed bytes/packets and per-set element counts.
Both would need the firewall counters or a netlink set dump to be read on every
scrape (through a subprocess or a kernel dump, with a different mechanism per
firewall backend), which is deliberately kept out of the daemon.

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

### Grafana dashboard and alerts

Import [keen-pbr-dashboard.json](https://keen-pbr.fyi/grafana/keen-pbr-dashboard.json)
into Grafana (Dashboards - New - Import). It expects a Prometheus data source
with UID `prometheus`, and has `job` and `outbound` variables, a 30 s refresh,
and the rows Status, Outbounds, DNS interception, Sets & lists and Errors. Use a
scrape interval of 30 s or less: the bar panels compute `increase()` over
one-minute windows.

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
- If refreshed data changed and affects active routing/DNS while the runtime is running, keen-pbr rebuilds runtime state so updates take effect immediately.

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
- `reloaded` *(boolean)*: Whether the running routing runtime was rebuilt because relevant changed lists were in active use.

### Status / Error Behavior

- `200`: Refresh operation completed.
- `400`: Requested list exists but is not URL-backed.
- `404`: Requested list not found.
- `409`: Refresh rejected because a staged draft exists or another config/runtime operation is already in progress.

Error response body:

```json
{
  "error": "human-readable message"
}
```

---

## GET /api/config

Returns the current configuration and a flag indicating whether a staged in-memory draft exists.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/config
```

### Response

```json
{
  "config": {
    "daemon": { "pid_file": "/var/run/keen-pbr.pid", "cache_dir": "/var/cache/keen-pbr" },
    "api": { "enabled": true, "listen": "127.0.0.1:12121" },
    "outbounds": [],
    "lists": {},
    "route": {}
  },
  "is_draft": false
}
```

`is_draft` is `true` when a config has been staged via `POST /api/config` but not yet saved to disk.

### Error Response (500)

```json
{
  "error": "Cannot open config file"
}
```

---

## POST /api/config

Validates the provided JSON body as a config file and stages it in memory. The config is **not** written to disk and the routing runtime is **not** changed. Use `POST /api/config/save` to persist and apply the staged draft.

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

Persists the staged config to disk, then applies it to the routing runtime.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config/save
```

### Response

```json
{
  "status": "ok",
  "message": "Config saved and applied",
  "saved": true,
  "applied": true,
  "rolled_back": false
}
```

### Error Response (400 — no staged config)

```json
{
  "error": "No staged config to save",
  "saved": false,
  "applied": false,
  "rolled_back": false
}
```

---

## GET /api/runtime/outbounds

Returns the daemon's current outbound runtime state: live urltest selection, interface reachability, and circuit breaker status.

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
        { "name": "tun0", "status": "up" }
      ]
    },
    {
      "tag": "auto_select",
      "type": "urltest",
      "status": "healthy",
      "selected_outbound": "vpn"
    }
  ]
}
```

---

## POST /api/routing/test

Resolves the target (if a domain), scans configured route rules against cached list data to determine the expected outbound, and queries the live kernel firewall sets to determine the actual outbound. Useful for diagnosing routing mismatches without restarting the daemon.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/routing/test \
  -H "Content-Type: application/json" \
  -d '{"target": "example.com"}'
```

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
  ]
}
```

---

## GET /api/health/routing

Verifies the live kernel routing and firewall state against the expected configuration. Checks that the firewall chain exists, all rules are present, route tables are populated, and policy rules are in place.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
```

### Response

```json
{
  "overall": "ok",
  "firewall_backend": "nftables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true,
    "detail": "chain keen-pbr found in table mangle"
  },
  "firewall_rules": [
    {
      "set_name": "keen-pbr-my_domains",
      "action": "MARK",
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

Streams the daemon's traffic interception events as Server-Sent Events. Each
connection receives `HELLO` first. The default `show=keen-pbr` view delivers
only marker events and closes after the matching marker; `show=all` (or the
compatibility alias `show=full`) stays open and delivers DNS, TLS SNI, HTTP
Host, QUIC Initial, and marker observations, including domains that did not
match a configured list. The legacy `DNS` events of the removed
`dns.dns_test_server` probe are no longer emitted. Pass `domain=<generated
marker>` with the default view to keep unrelated checks from closing it.

```bash {filename="bash"}
curl -N 'http://127.0.0.1:12121/api/dns/test?show=all'
```

### Stream Example

```text
data: {"type":"HELLO"}

data: {"type":"INTERCEPT","seq":42,"ts_ms":1712345678123,"source":"dns","client_ip":"192.168.1.10","domain":"example.com","lists":["streaming"],"ips":["203.0.113.7"],"added":1,"refreshed":0,"errors":0,"hold_us":180,"timed_out":false}

```
