# keen-pbr REST API Design Plan

## Overview

This document outlines the design for a REST API to manage keen-pbr configuration dynamically. The API will allow full CRUD operations on lists, ipsets, and general settings, plus status monitoring.

**Version**: v1
**Base Path**: `/api/v1`
**Format**: JSON
**Authentication**: None (initially - runs on localhost)

---

## API Endpoints

### 1. Lists Management (`/v1/lists`)

Lists define sources of domains/IPs/CIDRs that can be referenced by ipsets.

#### 1.1 List All Lists
```
GET /api/v1/lists
```

**Response** (200 OK):
```json
{
  "lists": [
    {
      "list_name": "local",
      "type": "hosts",
      "hosts": ["ifconfig.co", "myip2.ru"]
    },
    {
      "list_name": "local-file",
      "type": "file",
      "file": "/opt/etc/keen-pbr/local.lst"
    },
    {
      "list_name": "epic-games",
      "type": "url",
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/epicgames"
    }
  ]
}
```

**Fields**:
- `list_name` (string, required): Unique identifier for the list
- `type` (string, readonly): List type - "url", "file", or "hosts"
- `url` (string, optional): URL to download list from
- `file` (string, optional): Path to local file
- `hosts` (array of strings, optional): Inline list of domains/IPs

#### 1.2 Get Single List
```
GET /api/v1/lists/{list_name}
```

**Response** (200 OK):
```json
{
  "list_name": "local",
  "type": "hosts",
  "hosts": ["ifconfig.co", "myip2.ru"]
}
```

**Error Responses**:
- 404 Not Found: List does not exist

#### 1.3 Create List
```
POST /api/v1/lists
Content-Type: application/json
```

**Request Body**:
```json
{
  "list_name": "custom-domains",
  "hosts": ["example.com", "test.com"]
}
```

or

```json
{
  "list_name": "external-list",
  "url": "https://example.com/ips.txt"
}
```

or

```json
{
  "list_name": "local-file-list",
  "file": "/opt/etc/keen-pbr/custom.lst"
}
```

**Response** (201 Created):
```json
{
  "list_name": "custom-domains",
  "type": "hosts",
  "hosts": ["example.com", "test.com"]
}
```

**Error Responses**:
- 400 Bad Request: Invalid request body, missing required fields, or multiple sources specified
- 409 Conflict: List with same name already exists

**Validation Rules**:
- `list_name` is required and must be unique
- Exactly one of `url`, `file`, or `hosts` must be specified
- `hosts` must be non-empty array if specified

#### 1.4 Update List
```
PUT /api/v1/lists/{list_name}
Content-Type: application/json
```

**Request Body** (full replacement):
```json
{
  "hosts": ["newdomain.com", "another.com", "third.com"]
}
```

**Response** (200 OK):
```json
{
  "list_name": "custom-domains",
  "type": "hosts",
  "hosts": ["newdomain.com", "another.com", "third.com"]
}
```

**Error Responses**:
- 400 Bad Request: Invalid request body
- 404 Not Found: List does not exist

**Notes**:
- Cannot change `list_name` (use DELETE + POST instead)
- Can change list type by specifying different source (url/file/hosts)
- For inline lists (type="hosts"), this allows updating content directly

#### 1.5 Delete List
```
DELETE /api/v1/lists/{list_name}
```

**Response** (204 No Content)

**Error Responses**:
- 404 Not Found: List does not exist
- 409 Conflict: List is referenced by one or more ipsets (return list of ipsets in error message)

---

### 2. IPSets Management (`/v1/ipsets`)

IPSets define collections of IPs/domains with routing configuration.

#### 2.1 List All IPSets
```
GET /api/v1/ipsets
```

**Response** (200 OK):
```json
{
  "ipsets": [
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

#### 2.2 Get Single IPSet
```
GET /api/v1/ipsets/{ipset_name}
```

**Response** (200 OK):
```json
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
```

**Error Responses**:
- 404 Not Found: IPSet does not exist

#### 2.3 Create IPSet
```
POST /api/v1/ipsets
Content-Type: application/json
```

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
```

**Error Responses**:
- 400 Bad Request: Invalid request body or validation errors
- 409 Conflict: IPSet with same name already exists

**Validation Rules**:
- `ipset_name` must match regex `^[a-z][a-z0-9_]*$`
- `ip_version` must be 4 or 6
- All referenced lists must exist
- `fwmark`, `table`, `priority` must be positive integers
- `interfaces` must be non-empty array
- `override_dns` must be valid IP address format if specified

#### 2.4 Update IPSet
```
PUT /api/v1/ipsets/{ipset_name}
Content-Type: application/json
```

**Request Body** (full replacement):
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
```

**Error Responses**:
- 400 Bad Request: Invalid request body or validation errors
- 404 Not Found: IPSet does not exist

**Notes**:
- Cannot change `ipset_name` (use DELETE + POST instead)
- All fields are replaced (PUT semantic)
- `iptables_rules` currently not modifiable via API (future enhancement)

#### 2.5 Delete IPSet
```
DELETE /api/v1/ipsets/{ipset_name}
```

**Response** (204 No Content)

**Error Responses**:
- 404 Not Found: IPSet does not exist

---

### 3. General Settings (`/v1/general`)

Global configuration settings.

#### 3.1 Get General Settings
```
GET /api/v1/general
```

**Response** (200 OK):
```json
{
  "lists_output_dir": "/opt/etc/keen-pbr/lists.d",
  "use_keenetic_dns": true,
  "fallback_dns": "8.8.8.8"
}
```

**Fields**:
- `lists_output_dir` (string): Directory for downloaded lists
- `use_keenetic_dns` (boolean): Use Keenetic DNS from System profile as upstream in dnsmasq
- `fallback_dns` (string): Fallback DNS server (e.g., "8.8.8.8", "1.1.1.1", or empty string to disable)

#### 3.2 Update General Settings
```
POST /api/v1/general
Content-Type: application/json
```

**Request Body** (partial update allowed):
```json
{
  "use_keenetic_dns": false,
  "fallback_dns": "1.1.1.1"
}
```

**Response** (200 OK):
```json
{
  "lists_output_dir": "/opt/etc/keen-pbr/lists.d",
  "use_keenetic_dns": false,
  "fallback_dns": "1.1.1.1"
}
```

**Error Responses**:
- 400 Bad Request: Invalid request body

**Notes**:
- Partial updates supported (only specified fields are updated)
- `lists_output_dir` can be updated but requires restart of keen-pbr service
- `fallback_dns` must be valid IP address if specified, or empty string

---

### 4. Status Information (`/v1/status`)

System status and health checks.

#### 4.1 Get System Status
```
GET /api/v1/status
```

**Response** (200 OK):
```json
{
  "keen_pbr_version": "2.2.2",
  "keenetic_os_version": "3.9.5",
  "dnsmasq_status": "alive",
  "services": {
    "dnsmasq": {
      "status": "running",
      "message": "alive"
    }
  }
}
```

**Fields**:
- `keen_pbr_version` (string): Current keen-pbr version from VERSION file
- `keenetic_os_version` (string): Keenetic OS version from API, or "unknown" if unavailable
- `dnsmasq_status` (string): "alive", "dead", or "unknown"
- `services` (object): Detailed service status information

**Error Responses**:
- 500 Internal Server Error: Unable to determine status

**Status Check Implementation**:
- Version: Read from `/VERSION` file in project root
- Keenetic OS: Query Keenetic RCI API `/show version` or similar
- dnsmasq: Execute `/opt/etc/init.d/S56dnsmasq check` and parse output

---

## API Implementation Details

### Technology Stack

**Web Framework**:
- **chi** (github.com/go-chi/chi/v5) - lightweight, idiomatic, composable router
- Reasons:
  - Minimalist (no external dependencies)
  - Excellent middleware support
  - Context-based routing
  - Easy to test

**JSON Handling**: Standard library `encoding/json`

**Validation**: Custom validation using existing `config/validator.go` logic

### Project Structure

```
src/internal/api/
├── doc.go                  # Package documentation
├── server.go               # HTTP server setup and routing
├── handlers_lists.go       # List CRUD handlers
├── handlers_ipsets.go      # IPSet CRUD handlers
├── handlers_general.go     # General settings handlers
├── handlers_status.go      # Status handler
├── middleware.go           # Common middleware (logging, CORS, error handling)
├── responses.go            # Standard response helpers
├── errors.go               # API error types and responses
└── handlers_test.go        # API handler tests
```

### Configuration File Management

**Read-Modify-Write Pattern**:
1. Load config using `config.LoadConfig(path)`
2. Modify in-memory Config struct
3. Validate using `service.ValidationService`
4. Write back using `config.WriteConfig()`
5. Return response

**Concurrency**:
- Use mutex for config file writes
- Read operations can be concurrent
- Consider file locking for production (flock)

**Example Flow**:
```go
// Load
cfg, err := config.LoadConfig(configPath)
if err != nil {
    return err
}

// Modify
cfg.Lists = append(cfg.Lists, newList)

// Validate
validator := service.NewValidationService()
if err := validator.ValidateConfig(cfg); err != nil {
    return err
}

// Write
if err := cfg.WriteConfig(); err != nil {
    return err
}
```

### Error Handling

**Standard Error Response Format**:
```json
{
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Invalid ipset name: must match pattern ^[a-z][a-z0-9_]*$",
    "details": {
      "field": "ipset_name",
      "value": "Invalid-Name"
    }
  }
}
```

**Error Codes**:
- `VALIDATION_ERROR`: Input validation failed
- `NOT_FOUND`: Resource not found
- `CONFLICT`: Resource already exists or conflict
- `INTERNAL_ERROR`: Server error

### Middleware

1. **Logging Middleware**: Log all requests with method, path, status, duration
2. **Recovery Middleware**: Catch panics and return 500
3. **CORS Middleware**: Allow cross-origin requests (for web UI)
4. **Content-Type Middleware**: Enforce application/json

### Testing Strategy

1. **Unit Tests**: Test each handler with mock config
2. **Integration Tests**: Test full request/response cycle
3. **Validation Tests**: Test all validation rules
4. **Concurrent Access Tests**: Test thread safety

---

## Implementation Plan

### Phase 1: Foundation
1. Add `chi` router dependency to go.mod
2. Create API package structure
3. Implement basic server setup
4. Add middleware (logging, recovery, CORS)

### Phase 2: Lists Endpoints
1. Implement GET /api/v1/lists
2. Implement GET /api/v1/lists/{name}
3. Implement POST /api/v1/lists
4. Implement PUT /api/v1/lists/{name}
5. Implement DELETE /api/v1/lists/{name}
6. Add validation and tests

### Phase 3: IPSets Endpoints
1. Implement GET /api/v1/ipsets
2. Implement GET /api/v1/ipsets/{name}
3. Implement POST /api/v1/ipsets
4. Implement PUT /api/v1/ipsets/{name}
5. Implement DELETE /api/v1/ipsets/{name}
6. Add validation and tests

### Phase 4: General & Status Endpoints
1. Implement GET /api/v1/general
2. Implement POST /api/v1/general
3. Implement GET /api/v1/status
4. Add tests

### Phase 5: Integration
1. Add `api` command to keen-pbr CLI
2. Add configuration for API server (port, bind address)
3. Add systemd/init.d integration
4. Documentation and examples

---

## CLI Integration

### New Command: `api`

```bash
keen-pbr api [options]
```

**Options**:
- `--bind` (string): Bind address (default: "127.0.0.1:8080")
- `--config` (string): Config file path (default: "/opt/etc/keen-pbr/keen-pbr.conf")

**Example**:
```bash
# Start API server
keen-pbr api --bind 127.0.0.1:8080

# Start with custom config
keen-pbr api --config /custom/path/keen-pbr.conf --bind :9000
```

### Configuration File Addition

Add new section to `keen-pbr.conf`:

```toml
[api]
  # API server bind address (default: 127.0.0.1:8080)
  bind = "127.0.0.1:8080"
  # Enable API server
  enabled = false
```

---

## Security Considerations

### Current Design (Phase 1)
- **No Authentication**: API runs on localhost only
- **No Authorization**: All operations allowed
- **No TLS**: HTTP only

### Future Enhancements
1. **API Key Authentication**: Header-based API keys
2. **JWT Authentication**: Token-based auth
3. **TLS Support**: HTTPS with certificates
4. **Rate Limiting**: Prevent abuse
5. **Audit Logging**: Track all configuration changes
6. **Read-only Mode**: Separate read/write permissions

---

## Example Use Cases

### 1. Add Domain to Existing List

```bash
# Get current list
curl http://localhost:8080/api/v1/lists/local

# Update with new domain
curl -X PUT http://localhost:8080/api/v1/lists/local \
  -H "Content-Type: application/json" \
  -d '{"hosts": ["ifconfig.co", "myip2.ru", "newdomain.com"]}'
```

### 2. Create New IPSet for VPN

```bash
curl -X POST http://localhost:8080/api/v1/ipsets \
  -H "Content-Type: application/json" \
  -d '{
    "ipset_name": "streaming",
    "lists": ["netflix", "youtube"],
    "ip_version": 4,
    "flush_before_applying": true,
    "routing": {
      "interfaces": ["wg0"],
      "kill_switch": true,
      "fwmark": 2000,
      "table": 2000,
      "priority": 2000
    }
  }'
```

### 3. Update DNS Settings

```bash
curl -X POST http://localhost:8080/api/v1/general \
  -H "Content-Type: application/json" \
  -d '{"use_keenetic_dns": false, "fallback_dns": "1.1.1.1"}'
```

### 4. Check System Status

```bash
curl http://localhost:8080/api/v1/status | jq
```

---

## OpenAPI/Swagger Specification

Future enhancement: Generate OpenAPI 3.0 specification for:
- Interactive API documentation
- Client library generation
- API testing tools

---

## Testing Examples

### Manual Testing with curl

```bash
# List all lists
curl http://localhost:8080/api/v1/lists | jq

# Create new list
curl -X POST http://localhost:8080/api/v1/lists \
  -H "Content-Type: application/json" \
  -d '{"list_name": "test", "hosts": ["test.com"]}'

# Update list
curl -X PUT http://localhost:8080/api/v1/lists/test \
  -H "Content-Type: application/json" \
  -d '{"hosts": ["test.com", "test2.com"]}'

# Delete list
curl -X DELETE http://localhost:8080/api/v1/lists/test

# Get status
curl http://localhost:8080/api/v1/status | jq
```

---

## Performance Considerations

1. **Config File I/O**:
   - Cache config in memory
   - Only write on changes
   - Use atomic writes (write to temp + rename)

2. **Validation**:
   - Validate incrementally when possible
   - Cache validation results for read-only operations

3. **Status Checks**:
   - Cache status for 5 seconds
   - Async status updates
   - Timeout for external checks (2s)

4. **Concurrent Requests**:
   - Read lock for GET operations
   - Write lock for POST/PUT/DELETE
   - Queue writes to prevent conflicts

---

## Notes

- API does NOT automatically apply changes (user must run `keen-pbr apply`)
- Configuration is immediately persisted to file
- Consider adding `POST /api/v1/apply` endpoint to trigger application
- Consider adding `POST /api/v1/download` endpoint to trigger list downloads
- Consider WebSocket endpoint for real-time status updates

---

*Version: 1.0*
*Date: 2024-11-17*
*Status: Draft*
