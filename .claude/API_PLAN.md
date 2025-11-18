# keen-pbr REST API Design Plan - feature/3.0 Branch

## Overview

This comprehensive design document outlines a REST API for managing keen-pbr (policy-based routing) configuration dynamically on Keenetic routers. The implementation includes complete CRUD operations across lists, ipsets, and system settings.

**Adapted for feature/3.0 branch** which includes dependency injection architecture and improved project structure.

## Key Differences from Main Branch Implementation

### 1. Project Structure
- **Main branch**: `lib/` directory structure
- **feature/3.0**: `src/internal/` and `src/cmd/keen-pbr/` structure
- API code will be placed in: `src/internal/api/`
- Server command will be added to: `src/internal/commands/server.go`

### 2. Dependency Injection Integration
The feature/3.0 branch includes a comprehensive DI container in `src/internal/domain/`:
- **AppDependencies**: Centralized dependency container
- **Domain Interfaces**: KeeneticClient, NetworkManager, IPSetManager, etc.
- API handlers will receive dependencies via the container, not global state
- Enables proper testing with mock implementations

### 3. Command Pattern Integration
Existing commands implement the `commands.Runner` interface:
```go
type Runner interface {
    Name() string
    Init(args []string, ctx *AppContext) error
    Run() error
}
```

New API server will follow this pattern:
```go
commands.CreateServerCommand() // Returns a Runner
```

### 4. Configuration Access
Configuration types are already defined in `src/internal/config/types.go`:
- `Config` struct with `General`, `IPSets`, `Lists`
- Validation logic in `src/internal/config/validator.go`
- Config loading in `src/internal/config/config.go`

## Core API Structure

**Base Architecture**:
- Framework: chi router (lightweight, idiomatic Go)
- Format: JSON with standard data wrapper (`{"data": {...}}`)
- Authentication: None initially (localhost only)
- Base Path: `/api/v1`
- **New Dependency**: API handlers receive `*domain.AppDependencies`

## Six Main Endpoint Categories

### 1. Lists Management (`src/internal/api/lists.go`)
Handles domain/IP/CIDR sources with three types:
- `hosts`: Inline arrays of domains
- `file`: Local file paths
- `url`: Remote HTTP sources

**Implementation Notes for feature/3.0**:
- Uses existing `config.ListSource` type from `src/internal/config/types.go`
- Config loading via `config.LoadConfig()` function
- Validation via `config.ValidateConfig()` function
- No need to redefine types - they already exist!

Operations include full CRUD with validation ensuring unique names and exactly one source type per list.

### 2. IPSets Management (`src/internal/api/ipsets.go`)
Defines routable IP collections with associated configuration:
- List references
- IP version support (4 or 6)
- Routing parameters (interfaces, fwmark, table, priority)
- Kill switch options
- DNS override capabilities

**Implementation Notes for feature/3.0**:
- Uses existing `config.IPSetConfig` type from `src/internal/config/types.go`
- Leverages `domain.IPSetManager` interface for ipset operations
- Integrates with `domain.NetworkManager` for routing configuration

Validation enforces ipset name patterns matching `^[a-z][a-z0-9_]*$`.

### 3. General Settings (`src/internal/api/settings.go`)
Global configuration including:
- Lists output directory
- Keenetic DNS integration toggle
- Fallback DNS server specification

**Implementation Notes for feature/3.0**:
- Uses existing `config.GeneralConfig` type
- Supports partial updates with automatic service restart on changes

### 4. Status Information (`src/internal/api/status.go`)
System health monitoring returning:
- keen-pbr version
- Keenetic OS version
- Service statuses (dnsmasq, keen-pbr)
- Detailed service information

**Implementation Notes for feature/3.0**:
- Uses `domain.KeeneticClient.GetVersion()` for version info
- Integrates with existing service management in `src/internal/service/`
- No direct system calls - uses dependency-injected interfaces

### 5. Service Control (`src/internal/api/service.go`)
Manages keen-pbr daemon lifecycle via `{"up": true/false}` requests.

**Implementation Notes for feature/3.0**:
- Integrates with existing `src/internal/service/` package
- May leverage existing service management commands
- Uses init.d scripts for daemon control

### 6. Health Checks (`src/internal/api/health.go`)
Diagnostic endpoints including:
- Network configuration validation
- IPSet domain membership verification
- DNS resolution testing

**Implementation Notes for feature/3.0**:
- Uses `domain.NetworkManager` for network validation
- Leverages existing `src/internal/networking/config_checker.go`
- Integrates with `domain.KeeneticClient` for router health

## Implementation Architecture

### Process Separation
- **API Server**: HTTP interface managing configuration (new)
- **Routing Service**: Independent daemon handling policy-based routing (existing)
- **Communication**: Shared configuration file + init.d script invocation

### Configuration Management Pattern
The system follows read-modify-write semantics:
1. Load configuration using `config.LoadConfig(path)`
2. Apply requested modifications to in-memory `*config.Config`
3. Validate complete configuration using `config.ValidateConfig()`
4. Write back atomically using `config.SaveConfig()`
5. Restart services to apply changes (via service package)
6. Return updated state

### Automatic Service Restart
Configuration changes trigger: stop service → flush ipsets → start service → restart dnsmasq

### API Server Command (`src/internal/commands/server.go`)
New command implementing `commands.Runner`:
```go
type ServerCommand struct {
    bindAddr   string
    ctx        *AppContext
    deps       *domain.AppDependencies
}

func CreateServerCommand() commands.Runner {
    return &ServerCommand{}
}

func (c *ServerCommand) Name() string {
    return "server"
}

func (c *ServerCommand) Init(args []string, ctx *AppContext) error {
    // Parse -bind flag
    // Initialize AppDependencies
    return nil
}

func (c *ServerCommand) Run() error {
    // Start HTTP server with chi router
    // Wire up API handlers with dependencies
    return nil
}
```

Add to main.go:
```go
cmds := []commands.Runner{
    commands.CreateServiceCommand(),
    commands.CreateDownloadCommand(),
    commands.CreateApplyCommand(),
    commands.CreateDnsmasqConfigCommand(),
    commands.CreateInterfacesCommand(),
    commands.CreateSelfCheckCommand(),
    commands.CreateUndoCommand(),
    commands.CreateUpgradeConfigCommand(),
    commands.CreateDnsCommand(),
    commands.CreateServerCommand(), // NEW
}
```

### API Handler Structure
Each handler file follows this pattern:
```go
package api

import (
    "github.com/maksimkurb/keen-pbr/src/internal/domain"
    "github.com/maksimkurb/keen-pbr/src/internal/config"
)

type Handler struct {
    configPath string
    deps       *domain.AppDependencies
}

func NewHandler(configPath string, deps *domain.AppDependencies) *Handler {
    return &Handler{
        configPath: configPath,
        deps:       deps,
    }
}
```

### Middleware Stack (`src/internal/api/middleware.go`)
- JSON content-type enforcement
- Error recovery with structured errors
- Request logging
- CORS headers (for localhost development)

## Key Technical Details

**Error Response Format**:
```json
{
  "error": {
    "code": "invalid_request",
    "message": "IPSet name must match pattern ^[a-z][a-z0-9_]*$",
    "details": {
      "field": "ipset_name",
      "value": "Invalid-Name"
    }
  }
}
```

Standard HTTP status codes: 400, 404, 409, 500

**Testing Strategy**:
- Unit tests with mock dependencies from `src/internal/mocks/`
- Integration tests using `domain.NewTestDependencies()`
- Leverage existing test patterns in feature/3.0 branch
- Test coverage for all CRUD operations

**Project Structure**:
```
src/
├── cmd/
│   └── keen-pbr/
│       └── main.go                  # Add CreateServerCommand()
├── internal/
│   ├── api/                         # NEW
│   │   ├── handlers.go              # Main handler struct
│   │   ├── router.go                # Chi router setup
│   │   ├── middleware.go            # Middleware stack
│   │   ├── lists.go                 # Lists endpoints
│   │   ├── ipsets.go                # IPSets endpoints
│   │   ├── settings.go              # Settings endpoints
│   │   ├── status.go                # Status endpoints
│   │   ├── service.go               # Service control endpoints
│   │   ├── health.go                # Health check endpoints
│   │   ├── types.go                 # API request/response types
│   │   └── errors.go                # Error handling utilities
│   ├── commands/
│   │   └── server.go                # NEW - Server command
│   ├── domain/                      # EXISTING
│   │   ├── interfaces.go            # Domain interfaces
│   │   └── container.go             # DI container
│   ├── config/                      # EXISTING
│   │   ├── types.go                 # Config types (reuse!)
│   │   ├── config.go                # Config loading
│   │   └── validator.go             # Config validation
│   └── ...                          # Other existing packages
```

Estimated ~1,300 lines of new code (excluding tests).

## Usage Example

```bash
# Start API server
keen-pbr -config /opt/etc/keen-pbr/keen-pbr.conf server -bind 127.0.0.1:8080

# Check status
curl http://127.0.0.1:8080/api/v1/status

# List all ipsets
curl http://127.0.0.1:8080/api/v1/ipsets

# Add a new list
curl -X POST http://127.0.0.1:8080/api/v1/lists \
  -H "Content-Type: application/json" \
  -d '{"data": {"list_name": "mylist", "url": "https://example.com/list.txt"}}'
```

## Implementation Status

**To be implemented on feature/3.0 branch**:
- [ ] Server command (`src/internal/commands/server.go`)
- [ ] API package structure (`src/internal/api/`)
- [ ] Chi router dependency (add to `go.mod`)
- [ ] All six API handler categories
- [ ] Middleware stack
- [ ] Integration with existing DI container
- [ ] Unit tests using mock dependencies
- [ ] Integration tests
- [ ] Documentation updates

## Migration Notes from Main Branch

If API code was previously implemented on main branch:
1. **DO NOT directly port** - significant architectural differences
2. **Rewrite handlers** to use `domain.AppDependencies`
3. **Leverage existing types** from `src/internal/config/types.go`
4. **Follow existing patterns** in feature/3.0 commands
5. **Update imports** from `lib/` to `src/internal/`
6. **Remove global state** - use DI container exclusively

## Future Enhancements

- OpenAPI specification generation
- Authentication mechanisms (JWT, API keys)
- TLS support
- Rate limiting
- Audit logging capabilities
- WebSocket support for real-time status updates
- Metrics and monitoring endpoints (Prometheus)

## Summary of Changes from Original Plan

### ✅ Structural Changes
1. **Package path**: `lib/api/` → `src/internal/api/`
2. **Command integration**: Added `CreateServerCommand()` following existing pattern
3. **Dependency injection**: Handlers receive `*domain.AppDependencies` instead of using global state

### ✅ Architectural Improvements
1. **Leveraged existing types**: Reuse `config.Config`, `config.IPSetConfig`, `config.ListSource`
2. **Interface-based design**: Use domain interfaces for testability
3. **Consistent patterns**: Follow existing command and DI patterns
4. **Better testing**: Mock-friendly architecture with `NewTestDependencies()`

### ✅ Integration Points
1. **Configuration**: Use existing `config` package functions
2. **Networking**: Use `domain.NetworkManager` and `domain.IPSetManager`
3. **Keenetic API**: Use `domain.KeeneticClient` interface
4. **Service control**: Integrate with existing `service` package

### 📝 Documentation Updates Needed
1. Update main `README.md` with `server` command usage
2. Add API endpoint documentation
3. Update configuration examples if needed
4. Add troubleshooting section for API server

This plan is specifically tailored for the feature/3.0 branch and takes full advantage of its improved architecture while maintaining consistency with existing code patterns.
