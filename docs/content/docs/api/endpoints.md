---
title: Endpoints
weight: 1
---

All endpoints are served at the configured `api.listen` address (default `127.0.0.1:8080`).

---

## GET /api/health/service

Returns the running daemon version, overall status, and health information for every configured outbound. For `urltest` outbounds, includes per-child probe results, latencies, circuit breaker states, and the currently selected outbound.

```bash
curl http://127.0.0.1:8080/api/health/service
```

### Response

```json
{
  "version": "3.0.0",
  "status": "running",
  "resolver_config_hash": "a3f7c1d9e2b84560abcdef1234567890",
  "outbounds": [
    {
      "tag": "vpn",
      "type": "interface",
      "status": "healthy"
    },
    {
      "tag": "auto-select",
      "type": "urltest",
      "status": "healthy",
      "selected_outbound": "vpn",
      "children": [
        {
          "tag": "vpn",
          "success": true,
          "latency_ms": 42,
          "circuit_breaker": "closed"
        },
        {
          "tag": "wan",
          "success": true,
          "latency_ms": 5,
          "circuit_breaker": "closed"
        }
      ]
    }
  ]
}
```

**Outbound status values:**
- `healthy` — outbound is functioning (for urltest: a child is selected)
- `degraded` — urltest has no selected outbound
- `unknown` — urltest state not yet initialized

**Circuit breaker states:**
- `closed` — healthy, traffic passes through
- `open` — failed, blocked during cooldown
- `half_open` — testing recovery with limited probes

`resolver_config_hash` is an MD5 hex digest of the current domain-to-ipset mapping. Use it to verify the dnsmasq config is up to date.

---

## POST /api/reload

Triggers an asynchronous full reload: re-downloads all configured lists and re-applies firewall and routing rules.

```bash
curl -X POST http://127.0.0.1:8080/api/reload
```

### Response

```json
{
  "status": "ok",
  "message": "Reload triggered"
}
```

---

## GET /api/config

Returns the raw contents of the current configuration file as JSON.

```bash
curl http://127.0.0.1:8080/api/config
```

### Response

```json
{
  "daemon": { "pid_file": "/var/run/keen-pbr3.pid", "cache_dir": "/var/cache/keen-pbr3" },
  "api": { "enabled": true, "listen": "127.0.0.1:8080" },
  "outbounds": [...],
  "lists": {...},
  "route": {...}
}
```

### Error Response (500)

```json
{
  "error": "Cannot open config file"
}
```

---

## POST /api/config

Validates the provided JSON body as a config file, writes it atomically, then triggers a full reload.

```bash
curl -X POST http://127.0.0.1:8080/api/config \
  -H "Content-Type: application/json" \
  -d @new-config.json
```

### Response

```json
{
  "status": "ok",
  "message": "Config updated and reload triggered"
}
```

### Error Response (500)

```json
{
  "error": "Validation or write error message"
}
```

---

## GET /api/health/routing

Verifies the live kernel routing and firewall state against the expected configuration. Checks that the firewall chain exists, all rules are present, route tables are populated, and policy rules are in place.

```bash
curl http://127.0.0.1:8080/api/health/routing
```

### Response

```json
{
  "overall": "ok",
  "firewall_backend": "nftables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true,
    "detail": "chain keen-pbr3 found in table mangle"
  },
  "firewall_rules": [
    {
      "set_name": "keen-pbr3-my-domains",
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

Streams DNS probe query names as Server-Sent Events. The connection receives
`HELLO` immediately, then one event for each DNS name queried against
`dns.test_server` while the SSE connection is open.

```bash
curl -N http://127.0.0.1:8080/api/dns/test
```

### Stream Example

```text
data: HELLO

data: example.com

data: connectivity-check.local

```
