# keen-pbr REST API Documentation

**Version**: 1.0
**Base URL**: `http://localhost:8080/api/v1`
**Format**: JSON
**Authentication**: None (localhost only)

---

## Response Format

### Success Response
All successful responses wrap data in a `data` field:
```json
{
  "data": { /* response payload */ }
}
```

### Error Response
All error responses use the following format:
```json
{
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable error message",
    "details": { /* optional additional context */ }
  }
}
```

**Error Codes**:
- `VALIDATION_ERROR`: Input validation failed
- `NOT_FOUND`: Resource not found
- `CONFLICT`: Resource already exists or dependency conflict
- `INTERNAL_ERROR`: Server error

---

## Lists Management

### GET /api/v1/lists
Get all configured lists.

**Response** (200 OK):
```json
{
  "data": [
    {
      "list_name": "local",
      "type": "hosts",
      "hosts": ["ifconfig.co", "myip2.ru"]
    },
    {
      "list_name": "local-file",
      "type": "file",
      "file": "/opt/etc/keen-pbr/local.lst"
    }
  ]
}
```

### GET /api/v1/lists/{list_name}
Get a single list by name.

**Response** (200 OK):
```json
{
  "data": {
    "list_name": "local",
    "type": "hosts",
    "hosts": ["ifconfig.co", "myip2.ru"]
  }
}
```

### POST /api/v1/lists
Create a new list.

**Request Body**:
```json
{
  "list_name": "custom-domains",
  "hosts": ["example.com", "test.com"]
}
```

**Response** (201 Created):
```json
{
  "data": {
    "list_name": "custom-domains",
    "type": "hosts",
    "hosts": ["example.com", "test.com"]
  }
}
```

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

### PUT /api/v1/lists/{list_name}
Update an existing list.

**Request Body**:
```json
{
  "hosts": ["newdomain.com", "another.com"]
}
```

**Response** (200 OK):
```json
{
  "data": {
    "list_name": "custom-domains",
    "type": "hosts",
    "hosts": ["newdomain.com", "another.com"]
  }
}
```

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

### DELETE /api/v1/lists/{list_name}
Delete a list.

**Response** (204 No Content)

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

---

## IPSets Management

### GET /api/v1/ipsets
Get all configured ipsets.

**Response** (200 OK):
```json
{
  "data": [
    {
      "ipset_name": "vpn1",
      "lists": ["local-file", "local"],
      "ip_version": 4,
      "flush_before_applying": true,
      "routing": {
        "interfaces": ["nwg0"],
        "kill_switch": false,
        "fwmark": 1001,
        "table": 1001,
        "priority": 1001,
        "override_dns": ""
      },
      "iptables_rules": []
    }
  ]
}
```

### GET /api/v1/ipsets/{ipset_name}
Get a single ipset by name.

**Response** (200 OK):
```json
{
  "data": {
    "ipset_name": "vpn1",
    "lists": ["local-file", "local"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["nwg0"],
      "kill_switch": false,
      "fwmark": 1001,
      "table": 1001,
      "priority": 1001,
      "override_dns": ""
    },
    "iptables_rules": []
  }
}
```

### POST /api/v1/ipsets
Create a new ipset.

**Request Body**:
```json
{
  "ipset_name": "vpn2",
  "lists": ["local"],
  "ip_version": 4,
  "flush_before_applying": true,
  "routing": {
    "interfaces": ["nwg1"],
    "kill_switch": true,
    "fwmark": 1002,
    "table": 1002,
    "priority": 1002,
    "override_dns": "1.1.1.1"
  }
}
```

**Response** (201 Created):
```json
{
  "data": {
    "ipset_name": "vpn2",
    "lists": ["local"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["nwg1"],
      "kill_switch": true,
      "fwmark": 1002,
      "table": 1002,
      "priority": 1002,
      "override_dns": "1.1.1.1"
    },
    "iptables_rules": []
  }
}
```

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

### PUT /api/v1/ipsets/{ipset_name}
Update an existing ipset.

**Request Body**:
```json
{
  "lists": ["local", "epic-games"],
  "ip_version": 4,
  "flush_before_applying": true,
  "routing": {
    "interfaces": ["nwg1", "nwg0"],
    "kill_switch": false,
    "fwmark": 1002,
    "table": 1002,
    "priority": 1002
  }
}
```

**Response** (200 OK):
```json
{
  "data": {
    "ipset_name": "vpn2",
    "lists": ["local", "epic-games"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["nwg1", "nwg0"],
      "kill_switch": false,
      "fwmark": 1002,
      "table": 1002,
      "priority": 1002,
      "override_dns": ""
    },
    "iptables_rules": []
  }
}
```

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

### DELETE /api/v1/ipsets/{ipset_name}
Delete an ipset.

**Response** (204 No Content)

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

---

## General Settings

### GET /api/v1/general
Get general configuration settings.

**Response** (200 OK):
```json
{
  "data": {
    "lists_output_dir": "/opt/etc/keen-pbr/lists.d",
    "use_keenetic_dns": true,
    "fallback_dns": "8.8.8.8"
  }
}
```

### POST /api/v1/general
Update general settings (partial update supported).

**Request Body**:
```json
{
  "use_keenetic_dns": false,
  "fallback_dns": "1.1.1.1"
}
```

**Response** (200 OK):
```json
{
  "data": {
    "lists_output_dir": "/opt/etc/keen-pbr/lists.d",
    "use_keenetic_dns": false,
    "fallback_dns": "1.1.1.1"
  }
}
```

**Behavior**: Triggers service restart (keen-pbr + dnsmasq), flushes all ipsets.

---

## Status Information

### GET /api/v1/status
Get system status and health information.

**Response** (200 OK):
```json
{
  "data": {
    "keen_pbr_version": "2.2.2",
    "keenetic_os_version": "3.9.5",
    "dnsmasq_status": "alive",
    "keen_pbr_service_status": "running",
    "services": {
      "dnsmasq": {
        "status": "running",
        "message": "alive"
      },
      "keen_pbr": {
        "status": "running",
        "pid": 12345
      }
    }
  }
}
```

---

## Service Control

### POST /api/v1/service
Start or stop the keen-pbr routing service.

**Request Body**:
```json
{
  "up": true
}
```

**Response** (200 OK):
```json
{
  "data": {
    "status": "running",
    "message": "Service started successfully"
  }
}
```

---

## Health Checks

### GET /api/v1/check/networking
Run comprehensive networking configuration checks.

**Response** (200 OK):
```json
{
  "data": {
    "checks": [
      {
        "name": "config_valid",
        "description": "Configuration file is valid",
        "status": true,
        "message": "Configuration validated successfully"
      },
      {
        "name": "iptables_rules",
        "description": "IPTables rules are applied",
        "status": false,
        "message": "Missing iptables rule for ipset vpn1"
      }
    ],
    "overall_status": false,
    "failed_checks": ["iptables_rules"]
  }
}
```

### GET /api/v1/check/ipset
Check if a domain's resolved IPs are in a specific ipset.

**Query Parameters**:
- `ipset` (required): Name of the ipset
- `domain` (required): Domain name to check

**Example**: `GET /api/v1/check/ipset?ipset=vpn1&domain=ifconfig.co`

**Response** (200 OK):
```json
{
  "data": {
    "domain": "ifconfig.co",
    "ipset": "vpn1",
    "nameserver": "192.168.1.1",
    "results": [
      {
        "ip": "34.160.111.145",
        "in_set": true
      }
    ],
    "all_in_set": true
  }
}
```

---

## Service Restart Behavior

All configuration-modifying endpoints (POST, PUT, DELETE on lists/ipsets/general) trigger automatic service restart:

1. Stop keen-pbr service: `/opt/etc/init.d/S80keen-pbr stop`
2. **Flush all ipsets**: `ipset flush <name>` for each (ensures clean state)
3. Start keen-pbr service: `/opt/etc/init.d/S80keen-pbr start`
4. **Restart dnsmasq**: `/opt/etc/init.d/S56dnsmasq restart` (re-reads domain lists)

---

## Examples

### Create List
```bash
curl -X POST http://localhost:8080/api/v1/lists \
  -H "Content-Type: application/json" \
  -d '{"list_name": "test", "hosts": ["test.com"]}' | jq
```

### Create IPSet
```bash
curl -X POST http://localhost:8080/api/v1/ipsets \
  -H "Content-Type: application/json" \
  -d '{
    "ipset_name": "streaming",
    "lists": ["netflix"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["wg0"],
      "kill_switch": true,
      "fwmark": 2000,
      "table": 2000,
      "priority": 2000
    }
  }' | jq
```

### Update Settings
```bash
curl -X POST http://localhost:8080/api/v1/general \
  -H "Content-Type: application/json" \
  -d '{"use_keenetic_dns": false, "fallback_dns": "1.1.1.1"}' | jq
```

### Check Status
```bash
curl http://localhost:8080/api/v1/status | jq
```

### Check Domain
```bash
curl "http://localhost:8080/api/v1/check/ipset?ipset=vpn1&domain=ifconfig.co" | jq
```

---

*Version: 1.0*
*Date: 2024-11-17*
*Status: Complete API specification*
