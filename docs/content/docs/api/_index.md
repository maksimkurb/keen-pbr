---
title: API
weight: 4
---

keen-pbr includes an embedded HTTP REST API for monitoring, health checking, and configuration management.

## Availability

The API is available when:
- The binary was built with `with_api=true` (the default)
- The config has `api.enabled: true`
- The `--no-api` flag was not passed at startup

## Configuration

```json
{
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121"
  }
}
```

The default listen address is `0.0.0.0:12121`.

## Endpoints

See [Endpoints](endpoints/) for full documentation of all available endpoints.

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/health/service` | Daemon version, routing runtime status, and outbound health |
| `POST` | `/api/service/start` | Start the routing runtime |
| `POST` | `/api/service/stop` | Stop the routing runtime |
| `POST` | `/api/service/restart` | Restart the routing runtime |
| `GET` | `/api/config` | Get current config as JSON |
| `POST` | `/api/config` | Validate and stage config in memory |
| `POST` | `/api/config/save` | Persist and apply the staged config |
| `GET` | `/api/health/routing` | Live kernel routing/firewall verification |
