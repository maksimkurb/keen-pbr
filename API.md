# keen-pbr REST API Documentation

The keen-pbr REST API provides dynamic configuration management for policy-based routing on Keenetic routers. This API enables full CRUD operations on lists, ipsets, and general settings, along with system monitoring and service control capabilities.

## Table of Contents

- [Getting Started](#getting-started)
- [Authentication](#authentication)
- [Request/Response Format](#requestresponse-format)
- [Error Handling](#error-handling)
- [API Endpoints](#api-endpoints)
  - [Lists Management](#lists-management)
  - [IPSets Management](#ipsets-management)
  - [Settings Management](#settings-management)
  - [Status Monitoring](#status-monitoring)
  - [Service Control](#service-control)
  - [Health Checks](#health-checks)
  - [Network Diagnostics](#network-diagnostics)
- [Data Types](#data-types)
- [Examples](#examples)

## Getting Started

### Starting the API Server

```bash
# Start with default configuration
keen-pbr -config /opt/etc/keen-pbr/keen-pbr.conf server -bind 127.0.0.1:8080

# Start with custom bind address
keen-pbr -config /path/to/config.conf server -bind 0.0.0.0:9000

# Enable verbose logging
keen-pbr -config /opt/etc/keen-pbr/keen-pbr.conf -verbose server -bind 127.0.0.1:8080
```

### Base URL

All API endpoints are prefixed with `/api/v1`:

```
http://127.0.0.1:8080/api/v1
```

## Authentication

Currently, the API does not implement authentication. **For security, the server should only be bound to localhost (127.0.0.1)** or behind a reverse proxy with proper authentication.

## Request/Response Format

### Field Naming Convention

**All API field names use snake_case** (e.g., `ipset_name`, `lists_output_dir`, `use_keenetic_dns`).

This convention applies to:
- Request bodies (POST, PUT, PATCH)
- Response bodies (GET, POST, PUT, PATCH)
- Query parameters
- Path parameters

**Example:**
```json
{
  "ipset_name": "vpn1",
  "ip_version": 4,
  "flush_before_applying": true,
  "routing": {
    "interfaces": ["nwg0"],
    "kill_switch": true
  }
}
```

### Successful Responses

All successful responses return JSON with a `data` wrapper:

```json
{
  "data": {
    // Response content
  }
}
```

### HTTP Status Codes

- `200 OK` - Successful GET, PUT, PATCH requests
- `201 Created` - Successful POST request (resource created)
- `204 No Content` - Successful DELETE request
- `400 Bad Request` - Invalid request data or validation error
- `404 Not Found` - Resource not found
- `409 Conflict` - Resource conflict (e.g., duplicate name)
- `500 Internal Server Error` - Server error

## Error Handling

### Error Response Format

Errors return a structured JSON response:

```json
{
  "error": {
    "code": "error_code",
    "message": "Human-readable error message",
    "details": {
      // Optional additional details
    }
  }
}
```

### Error Codes

| Code | Description |
|------|-------------|
| `invalid_request` | Malformed or invalid request data |
| `not_found` | Requested resource not found |
| `conflict` | Resource conflict (duplicate name, referenced resource) |
| `validation_failed` | Configuration validation failed |
| `service_error` | Service operation failed |
| `internal_error` | Internal server error |

### Error Examples

**Invalid Request:**
```json
{
  "error": {
    "code": "invalid_request",
    "message": "list_name is required"
  }
}
```

**Validation Error:**
```json
{
  "error": {
    "code": "validation_failed",
    "message": "ipset_name must match pattern ^[a-z][a-z0-9_]*$",
    "details": {
      "field": "ipset_name",
      "value": "Invalid-Name"
    }
  }
}
```

**Conflict:**
```json
{
  "error": {
    "code": "conflict",
    "message": "List with name 'mylist' already exists"
  }
}
```

## API Endpoints

### Lists Management

Lists define sources of domains, IPs, or CIDRs used by ipsets.

#### Get All Lists

```http
GET /api/v1/lists
```

**Response:**
```json
{
  "data": {
    "lists": [
      {
        "list_name": "vpn-domains",
        "type": "url",
        "url": "https://example.com/vpn-list.txt",
        "stats": {
          "total_hosts": 1250,
          "ipv4_subnets": 45,
          "ipv6_subnets": 12,
          "downloaded": true,
          "last_modified": "2025-11-17T10:30:00Z"
        }
      },
      {
        "list_name": "local-ips",
        "type": "file",
        "file": "/opt/etc/keen-pbr/lists/local.txt",
        "stats": {
          "total_hosts": 0,
          "ipv4_subnets": 25,
          "ipv6_subnets": 0
        }
      },
      {
        "list_name": "inline-hosts",
        "type": "hosts",
        "stats": {
          "total_hosts": 2,
          "ipv4_subnets": 0,
          "ipv6_subnets": 0
        }
      }
    ]
  }
}
```

**Response Fields:**
- `list_name` - Name of the list
- `type` - List type: `url`, `file`, or `hosts`
- `url` - Source URL (only for URL-based lists)
- `file` - File path (only for file-based lists)
- `stats` - List statistics:
  - `total_hosts` - Number of domains/hostnames
  - `ipv4_subnets` - Number of IPv4 addresses/subnets
  - `ipv6_subnets` - Number of IPv6 addresses/subnets
  - `downloaded` - Whether the file has been downloaded (URL-based lists only)
  - `last_modified` - Last modification time in RFC3339 format (URL-based lists only)

**Notes:**
- Inline host lists are not returned in the response to avoid sending large arrays. Use the statistics instead.
- **Statistics are cached** for 5 minutes to improve performance. The cache is automatically invalidated when lists are modified via the API (create, update, delete operations).
- The cache is also invalidated if the list file's modification time changes.

#### Get Specific List

```http
GET /api/v1/lists/{name}
```

**Parameters:**
- `name` (path) - List name

**Response:**
```json
{
  "data": {
    "list_name": "vpn-domains",
    "type": "url",
    "url": "https://example.com/vpn-list.txt",
    "stats": {
      "total_hosts": 1250,
      "ipv4_subnets": 45,
      "ipv6_subnets": 12,
      "downloaded": true,
      "last_modified": "2025-11-17T10:30:00Z"
    }
  }
}
```

#### Create List

```http
POST /api/v1/lists
Content-Type: application/json
```

**Request Body:**
```json
{
  "list_name": "new-list",
  "url": "https://example.com/list.txt"
}
```

**List Types:**
- **URL-based:** `{"list_name": "name", "url": "https://..."}`
- **File-based:** `{"list_name": "name", "file": "/path/to/file.txt"}`
- **Inline hosts:** `{"list_name": "name", "hosts": ["domain1.com", "domain2.com"]}`

**Validation:**
- `list_name` is required
- Exactly one of `url`, `file`, or `hosts` must be specified
- List name must be unique

**Response:** `201 Created`
```json
{
  "data": {
    "list_name": "new-list",
    "url": "https://example.com/list.txt"
  }
}
```

#### Update List

```http
PUT /api/v1/lists/{name}
Content-Type: application/json
```

**Parameters:**
- `name` (path) - Current list name

**Request Body:**
```json
{
  "list_name": "updated-name",
  "url": "https://example.com/updated-list.txt"
}
```

**Response:** `200 OK`

#### Delete List

```http
DELETE /api/v1/lists/{name}
```

**Parameters:**
- `name` (path) - List name

**Validation:**
- List must not be referenced by any ipset

**Response:** `204 No Content`

### IPSets Management

IPSets define routing configurations for IP collections.

#### Get All IPSets

```http
GET /api/v1/ipsets
```

**Response:**
```json
{
  "data": {
    "ipsets": [
      {
        "ipset_name": "vpn_ipset",
        "lists": ["vpn-domains"],
        "ip_version": 4,
        "flush_before_applying": true,
        "routing": {
          "interfaces": ["nwg1", "nwg0"],
          "kill_switch": true,
          "fwmark": 100,
          "table": 100,
          "priority": 100,
          "override_dns": "1.1.1.1#53"
        }
      }
    ]
  }
}
```

#### Get Specific IPSet

```http
GET /api/v1/ipsets/{name}
```

**Parameters:**
- `name` (path) - IPSet name

**Response:**
```json
{
  "data": {
    "ipset_name": "vpn_ipset",
    "lists": ["vpn-domains"],
    "ip_version": 4,
    "routing": {
      "interfaces": ["nwg1"],
      "fwmark": 100,
      "table": 100,
      "priority": 100
    }
  }
}
```

#### Create IPSet

```http
POST /api/v1/ipsets
Content-Type: application/json
```

**Request Body:**
```json
{
  "ipset_name": "new_ipset",
  "lists": ["list1", "list2"],
  "ip_version": 4,
  "flush_before_applying": true,
  "routing": {
    "interfaces": ["nwg1"],
    "kill_switch": true,
    "fwmark": 200,
    "table": 200,
    "priority": 200,
    "override_dns": "8.8.8.8#53"
  }
}
```

**Field Descriptions:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `ipset_name` | string | Yes | IPSet name (must match `^[a-z][a-z0-9_]*$`) |
| `lists` | array | Yes | List of list names to include |
| `ip_version` | int | Yes | IP version: `4` or `6` |
| `flush_before_applying` | bool | No | Clear ipset before filling (default: false) |
| `routing.interfaces` | array | Yes | Interface names in priority order |
| `routing.kill_switch` | bool | No | Block traffic when all interfaces down (default: true) |
| `routing.fwmark` | int | Yes | Firewall mark for packets |
| `routing.table` | int | Yes | Routing table number |
| `routing.priority` | int | Yes | IP rule priority |
| `routing.override_dns` | string | No | Override DNS server (format: `server#port`) |

**Validation:**
- IPSet name must match pattern `^[a-z][a-z0-9_]*$`
- IP version must be 4 or 6
- All referenced lists must exist
- IPSet name must be unique

**Response:** `201 Created`

#### Update IPSet

```http
PUT /api/v1/ipsets/{name}
Content-Type: application/json
```

**Parameters:**
- `name` (path) - Current ipset name

**Request Body:** Same as Create IPSet

**Response:** `200 OK`

#### Delete IPSet

```http
DELETE /api/v1/ipsets/{name}
```

**Parameters:**
- `name` (path) - IPSet name

**Response:** `204 No Content`

### Settings Management

Manage global configuration settings.

#### Get Settings

```http
GET /api/v1/settings
```

**Response:**
```json
{
  "data": {
    "general": {
      "lists_output_dir": "lists.d",
      "use_keenetic_dns": true,
      "fallback_dns": "8.8.8.8"
    }
  }
}
```

#### Update Settings (Partial)

```http
PATCH /api/v1/settings
Content-Type: application/json
```

**Request Body (all fields optional):**
```json
{
  "lists_output_dir": "new-lists-dir",
  "use_keenetic_dns": false,
  "fallback_dns": "1.1.1.1"
}
```

**Field Descriptions:**

| Field | Type | Description |
|-------|------|-------------|
| `lists_output_dir` | string | Directory for downloaded lists |
| `use_keenetic_dns` | bool | Use Keenetic DNS from System profile |
| `fallback_dns` | string | Fallback DNS server (e.g., `8.8.8.8`) |

**Response:** `200 OK`
```json
{
  "data": {
    "general": {
      "lists_output_dir": "new-lists-dir",
      "use_keenetic_dns": false,
      "fallback_dns": "1.1.1.1"
    }
  }
}
```

### Status Monitoring

Get system status and version information.

#### Get Status

```http
GET /api/v1/status
```

**Response:**
```json
{
  "data": {
    "version": "2.2.2",
    "keenetic_version": "KeeneticOS 3.8",
    "services": {
      "keen-pbr": {
        "status": "running",
        "message": "Service is running"
      },
    }
  }
}
```

**Service Status Values:**
- `running` - Service is active
- `stopped` - Service is not running
- `unknown` - Unable to determine status

### Service Control

Control the keen-pbr service (start/stop).

#### Control Service

```http
POST /api/v1/service
Content-Type: application/json
```

**Request Body:**
```json
{
  "up": true
}
```

**Parameters:**
- `up` (bool) - `true` to start service, `false` to stop

**Response:** `200 OK`
```json
{
  "data": {
    "status": "success",
    "message": "Service start command executed successfully"
  }
}
```

### Health Checks

Perform comprehensive health checks.

#### Check Health

```http
GET /api/v1/health
```

**Response:**
```json
{
  "data": {
    "healthy": true,
    "checks": {
      "config_validation": {
        "passed": true,
        "message": "Configuration is valid"
      },
      "network_config": {
        "passed": true,
        "message": "Network configuration is valid"
      },
      "keenetic_connectivity": {
        "passed": true,
        "message": "Keenetic API is accessible"
      }
    }
  }
}
```

**Health Checks Performed:**
1. **config_validation** - Configuration file is valid
2. **network_config** - Network interfaces exist and are configured correctly
3. **keenetic_connectivity** - Keenetic router API is accessible

### Network Diagnostics

Perform real-time network diagnostics and routing checks.

#### Check Routing

Check how a specific host is routed through the current configuration.

```http
POST /api/v1/check/routing
Content-Type: application/json
```

**Request Body:**
```json
{
  "host": "example.com"
}
```

**Response:**
```json
{
  "data": {
    "host": "example.com",
    "resolved_ips": ["93.184.216.34"],
    "matched_by_hostname": [
      {
        "rule_name": "vpn_routes",
        "pattern": "example.com"
      }
    ],
    "ipset_checks": [
      {
        "ip": "93.184.216.34",
        "rule_results": [
          {
            "rule_name": "vpn_routes",
            "present_in_ipset": true,
            "should_be_present": true,
            "match_reason": "hostname match"
          }
        ]
      }
    ]
  }
}
```

#### Ping (SSE)

Stream ping results for a host via Server-Sent Events (SSE).

```http
GET /api/v1/check/ping?host=example.com
```

**Response (Stream):**
```
data: PING example.com (93.184.216.34): 56 data bytes
data: 64 bytes from 93.184.216.34: seq=0 ttl=56 time=15.234 ms
...
```

#### Traceroute (SSE)

Stream traceroute results for a host via SSE.

```http
GET /api/v1/check/traceroute?host=example.com
```

**Response (Stream):**
```
data: traceroute to example.com (93.184.216.34), 30 hops max, 60 byte packets
data:  1  192.168.1.1 (192.168.1.1)  0.456 ms  0.345 ms  0.234 ms
...
```

#### Self Check (SSE)

Run a comprehensive self-check of the system configuration and state.

```http
GET /api/v1/check/self
```

**Response (Stream):**
```json
data: {"check":"config","ok":true,"log":"Configuration is valid","reason":"","command":""}
...
```

## Data Types

### ListInfo (Response)

```typescript
{
  "list_name": string,
  "type": "url" | "file" | "hosts",
  "url"?: string,        // HTTP(S) URL (only for url type)
  "file"?: string,       // Local file path (only for file type)
  "stats": {
    "total_hosts": number,           // Number of domains/hostnames
    "ipv4_subnets": number,          // Number of IPv4 addresses/subnets
    "ipv6_subnets": number,          // Number of IPv6 addresses/subnets
    "downloaded"?: boolean,          // File download status (url type only)
    "last_modified"?: string         // RFC3339 timestamp (url type only)
  }
}
```

**Note:** This is the response format for GET requests. Inline `hosts` arrays are not included in responses.

### ListSource (Request)

```typescript
{
  "list_name": string,
  "url"?: string,        // HTTP(S) URL to download list
  "file"?: string,       // Local file path
  "hosts"?: string[]     // Inline array of domains
}
```

**Constraints:**
- Exactly one of `url`, `file`, or `hosts` must be specified
- Use this format for POST and PUT requests

### IPSetConfig

```typescript
{
  "ipset_name": string,              // Pattern: ^[a-z][a-z0-9_]*$
  "lists": string[],                 // References to list names
  "ip_version": 4 | 6,
  "flush_before_applying": boolean,
  "routing": {
    "interfaces": string[],          // Interface names in priority order
    "kill_switch": boolean,          // Default: true
    "fwmark": number,
    "table": number,
    "priority": number,
    "override_dns": string           // Format: "server#port"
  },
  "iptables_rule"?: Array<{
    "chain": string,
    "table": string,
    "rule": string[]
  }>
}
```

### GeneralConfig

```typescript
{
  "lists_output_dir": string,
  "use_keenetic_dns": boolean,
  "fallback_dns": string
}
```

## Examples

### Complete Workflow: Create VPN Routing

**Step 1: Create a list of VPN domains**

```bash
curl -X POST http://127.0.0.1:8080/api/v1/lists \
  -H "Content-Type: application/json" \
  -d '{
    "list_name": "vpn_domains",
    "url": "https://raw.githubusercontent.com/user/repo/vpn-domains.txt"
  }'
```

**Step 2: Create an ipset for VPN routing**

```bash
curl -X POST http://127.0.0.1:8080/api/v1/ipsets \
  -H "Content-Type: application/json" \
  -d '{
    "ipset_name": "vpn_routes",
    "lists": ["vpn_domains"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["nwg1"],
      "kill_switch": true,
      "fwmark": 100,
      "table": 100,
      "priority": 100,
      "override_dns": "1.1.1.1#53"
    }
  }'
```

**Step 3: Check system status**

```bash
curl http://127.0.0.1:8080/api/v1/status
```

**Step 4: Perform health check**

```bash
curl http://127.0.0.1:8080/api/v1/health
```

### Update List URL

```bash
curl -X PUT http://127.0.0.1:8080/api/v1/lists/vpn_domains \
  -H "Content-Type: application/json" \
  -d '{
    "list_name": "vpn_domains",
    "url": "https://new-url.com/vpn-list.txt"
  }'
```

### Add Interface to IPSet

```bash
# First, get current configuration
curl http://127.0.0.1:8080/api/v1/ipsets/vpn_routes

# Then update with new interface added
curl -X PUT http://127.0.0.1:8080/api/v1/ipsets/vpn_routes \
  -H "Content-Type: application/json" \
  -d '{
    "ipset_name": "vpn_routes",
    "lists": ["vpn_domains"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["nwg1", "nwg0"],
      "kill_switch": true,
      "fwmark": 100,
      "table": 100,
      "priority": 100,
      "override_dns": "1.1.1.1#53"
    }
  }'
```

### Update Settings

```bash
curl -X PATCH http://127.0.0.1:8080/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{
    "use_keenetic_dns": true,
    "fallback_dns": "1.1.1.1"
  }'
```

### Restart Service

```bash
# Stop service
curl -X POST http://127.0.0.1:8080/api/v1/service \
  -H "Content-Type: application/json" \
  -d '{"up": false}'

# Start service
curl -X POST http://127.0.0.1:8080/api/v1/service \
  -H "Content-Type: application/json" \
  -d '{"up": true}'
```

### Delete Resources

```bash
# Delete ipset (must be done before deleting referenced lists)
curl -X DELETE http://127.0.0.1:8080/api/v1/ipsets/vpn_routes

# Delete list
curl -X DELETE http://127.0.0.1:8080/api/v1/lists/vpn_domains
```

## Configuration Persistence

All changes made through the API are immediately persisted to the configuration file specified when starting the server. The API follows this pattern:

1. Load configuration from disk
2. Apply requested modifications
3. Validate complete configuration
4. Save configuration atomically
5. Return updated state

**Note:** Changes to the configuration file require restarting the keen-pbr service to take effect. Use the `/api/v1/service` endpoint to restart the service programmatically.

## CORS Support

The API includes CORS headers for localhost development:
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization`

## Rate Limiting

Currently, no rate limiting is implemented. Consider implementing rate limiting at the reverse proxy level if exposing the API beyond localhost.

## Versioning

The API is versioned via the URL path (`/api/v1`). Future versions will use `/api/v2`, etc., allowing backward compatibility.

## Support

For issues, questions, or contributions, please visit the [keen-pbr GitHub repository](https://github.com/maksimkurb/keen-pbr).
