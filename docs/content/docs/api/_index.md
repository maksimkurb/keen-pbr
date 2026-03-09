---
title: API
weight: 4
---

keen-pbr3 includes an embedded HTTP REST API for monitoring, health checking, and configuration management.

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
    "listen": "127.0.0.1:8080"
  }
}
```

The default listen address is `127.0.0.1:8080`. To expose the API on all interfaces, use `0.0.0.0:8080`.

## Endpoints

See [Endpoints](endpoints/) for full documentation of all available endpoints.

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/health/service` | Daemon version, status, and outbound health |
| `POST` | `/api/reload` | Trigger async reload |
| `GET` | `/api/config` | Get current config as JSON |
| `POST` | `/api/config` | Validate, write, and reload config |
| `GET` | `/api/health/routing` | Live kernel routing/firewall verification |
