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
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121"
  }
}
```

By default, the API listens on `0.0.0.0:12121`. All endpoints are served at the configured `api.listen` address.

---

## GET /api/health/service

Returns the running daemon version, routing runtime status, and resolver configuration summary.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Response

```json
{
  "version": "3.0.0",
  "status": "running",
  "resolver_config_hash": "a3f7c1d9e2b84560abcdef1234567890",
  "resolver_config_hash_actual": "a3f7c1d9e2b84560abcdef1234567890",
  "config_is_draft": false
}
```

`resolver_config_hash` is an MD5 hex digest of the expected domain-to-ipset mapping derived from the current config. `resolver_config_hash_actual` reflects the hash of the config that was last applied to the running system resolver. When these two values differ, the dnsmasq config may be out of date.

For live outbound runtime state (health, latency, circuit breaker) use `GET /api/runtime/outbounds`.

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
  "reloaded": true
}
```

Success payload fields:

- `refreshed_lists` *(array[string])*: URL-backed lists that were refreshed.
- `changed_lists` *(array[string])*: Refreshed lists whose cached contents changed.
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
    { "path": "outbounds.vpn.interface", "message": "interface is required" }
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

Streams DNS queries observed by the built-in `dns.dns_test_server` listener as Server-Sent Events. Each event payload is a JSON object. The connection receives a `HELLO` event immediately, then one `DNS` event per queried name while the connection is open.

```bash {filename="bash"}
curl -N http://127.0.0.1:12121/api/dns/test
```

### Stream Example

```text
data: {"type":"HELLO"}

data: {"type":"DNS","domain":"example.com","source_ip":"192.168.1.10","ecs":"203.0.113.0/24"}

data: {"type":"DNS","domain":"connectivity-check.local","source_ip":"192.168.1.11","ecs":null}

```
