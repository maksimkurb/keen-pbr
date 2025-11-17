# Keenetic Policy-Based Routing (keen-pbr) - Project Context

## Overview

**keen-pbr** is a policy-based routing toolkit for Keenetic routers written in Go. It enables selective traffic routing based on IP addresses, CIDR blocks, and domain names using ipset and dnsmasq integration.

**Language**: Go 1.23
**Module**: `github.com/maksimkurb/keen-pbr`
**License**: MIT
**Architecture**: Refactored modular design with service layer (November 2024)

---

## Project Structure

```
keen-pbr/
├── src/                              # Go source code (standard layout)
│   ├── cmd/                          # Application entry points
│   │   └── keen-pbr/
│   │       └── main.go               # CLI flag parsing and command dispatch
│   │
│   └── internal/                     # Private application packages (not importable)
│       ├── commands/                 # CLI command handlers (thin wrappers)
│       │   ├── doc.go                # Package documentation
│       │   ├── common.go             # Runner interface, AppContext, config loading
│       │   ├── apply.go              # Apply routing configuration
│       │   ├── download.go           # Download IP lists
│       │   ├── undo.go               # Remove routing configuration
│       │   ├── service.go            # Daemon mode with interface monitoring
│       │   ├── self_check.go         # Configuration validation
│       │   ├── dns.go                # DNS proxy profile viewer
│       │   ├── dnsmasq_config.go     # dnsmasq configuration generator
│       │   ├── interfaces.go         # Network interface lister
│       │   └── upgrade_config.go     # Configuration format upgrader
│       │
│       ├── service/                  # Business logic orchestration layer
│       │   ├── doc.go                # Package documentation
│       │   ├── routing_service.go    # Orchestrates routing operations
│       │   ├── ipset_service.go      # Orchestrates ipset operations
│       │   ├── validation_service.go # Centralized configuration validation
│       │   ├── *_test.go             # Service layer tests
│       │
│       ├── networking/               # Network configuration management
│       │   ├── doc.go                # Package documentation
│       │   ├── manager.go            # Main facade for network operations
│       │   ├── persistent.go         # Persistent config (iptables, ip rules)
│       │   ├── routing.go            # Dynamic routing config (ip routes)
│       │   ├── interface_selector.go # Best interface selection logic
│       │   ├── ipset_manager.go      # IPSet manager (domain interface impl)
│       │   ├── builders.go           # Builder patterns for IPTables/IPRule
│       │   ├── ipset.go              # IPSet operations
│       │   ├── iptables.go           # IPTables rules management
│       │   ├── iproute.go            # IP route management
│       │   ├── iprule.go             # IP rule management
│       │   ├── interfaces.go         # Interface information
│       │   ├── config_checker.go     # Network state validation
│       │   ├── network.go            # Network utilities
│       │   ├── shell.go              # Shell command execution
│       │   └── *_test.go             # Networking layer tests
│       │
│       ├── keenetic/                 # Keenetic router API client
│       │   ├── doc.go                # Package documentation
│       │   ├── client.go             # RCI API client with caching
│       │   ├── version.go            # Version detection
│       │   ├── interfaces.go         # Interface retrieval
│       │   ├── dns.go                # DNS configuration
│       │   ├── cache.go              # Response caching
│       │   ├── http.go               # HTTP client abstraction
│       │   └── *_test.go             # API client tests
│       │
│       ├── domain/                   # Core interfaces and abstractions
│       │   ├── doc.go                # Package documentation
│       │   └── interfaces.go         # Domain interfaces for DI
│       │       ├── NetworkManager    # Facade for network operations
│       │       ├── RouteManager      # IP route management interface
│       │       ├── InterfaceProvider # Interface information provider
│       │       ├── IPSetManager      # IPSet operations interface
│       │       ├── KeeneticClient    # Router API client interface
│       │
│       ├── mocks/                    # Test doubles for unit testing
│       │   ├── doc.go                # Package documentation
│       │   ├── networking.go         # Mock networking implementations
│       │   ├── keenetic.go           # Mock Keenetic client
│       │   └── *_test.go             # Mock tests
│       │
│       ├── lists/                    # IP/domain list management
│       │   ├── doc.go                # Package documentation
│       │   ├── downloader.go         # HTTP list downloading
│       │   ├── common.go             # List iteration and parsing
│       │   ├── domain_store.go       # Domain storage
│       │   ├── ipset_importer.go     # Import IPs to ipset
│       │   ├── dnsmasq_generator.go  # Generate dnsmasq configs
│       │   └── *_test.go             # List processing tests
│       │
│       ├── config/                   # Configuration management
│       │   ├── doc.go                # Package documentation
│       │   ├── config.go             # TOML parsing and loading
│       │   ├── types.go              # Config data structures
│       │   ├── validator.go          # Config validation rules
│       │   └── *_test.go             # Config parsing tests
│       │
│       ├── errors/                   # Domain-specific error types
│       │   ├── doc.go                # Package documentation
│       │   └── errors.go             # Structured errors with codes
│       │
│       ├── utils/                    # General-purpose utilities
│       │   ├── doc.go                # Package documentation
│       │   ├── ips.go                # IP address conversion
│       │   ├── paths.go              # Path resolution
│       │   ├── files.go              # File operations
│       │   ├── validator.go          # DNS/domain validation
│       │   ├── bitset.go             # Bit manipulation
│       │   └── *_test.go             # Utility tests
│       │
│       ├── hashing/                  # MD5 checksum utilities
│       │   ├── doc.go                # Package documentation
│       │   ├── md5proxy.go           # Transparent checksum calculation
│       │   └── *_test.go             # Hashing tests
│       │
│       └── log/                      # Leveled logging
│           ├── doc.go                # Package documentation
│           ├── logger.go             # Colored console logging
│           └── *_test.go             # Logger tests
│
├── .claude/                          # Claude AI assistant context
│   ├── CONTEXT.md                    # This file - project documentation
│   └── PLAN.md                       # Refactoring plan (all phases complete)
│
├── .github/
│   └── workflows/
│       ├── build.yml                 # CI: Build binaries on every push
│       └── release.yml               # CI: Create releases (main + VERSION)
│
├── package/                          # Package building and installation
│   ├── entware/keen-pbr/Makefile     # Entware/OpenWrt package definition
│   └── etc/                          # Configuration files to install
│       ├── init.d/S80keen-pbr        # Init script for daemon
│       ├── cron.daily/50-keen-pbr-lists-update.sh  # Daily list updates
│       ├── ndm/                      # Keenetic NDM hooks
│       │   ├── netfilter.d/50-keen-pbr-fwmarks.sh
│       │   └── ifstatechanged.d/50-keen-pbr-routing.sh
│       ├── keen-pbr/                 # Configuration templates
│       └── dnsmasq.d/                # dnsmasq integration
│
├── go.mod                            # Go module definition
├── go.sum                            # Go dependency lock file
├── VERSION                           # Version file (manually managed)
├── Makefile                          # Build orchestration
├── packages.mk                       # Local IPK package building
├── repository.mk                     # Package repository generation
├── README.md / README.en.md          # Documentation
└── keen-pbr.example.conf             # Example configuration
```

---

## Architecture

### Layered Architecture (Post-Refactoring)

The application follows a clean layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                      CLI Layer (commands/)                   │
│  Thin wrappers: Parse args, call services, format output    │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                   Service Layer (service/)                   │
│  Business logic orchestration: Coordinate operations across │
│  multiple managers, enforce business rules                  │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              Domain Layer (networking/, keenetic/)           │
│  Core domain logic: Network operations, router interaction  │
│  Implements domain interfaces for dependency injection      │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│          Infrastructure (lists/, config/, utils/)            │
│  Support services: Config parsing, list processing, utils   │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Dependency Injection**: All dependencies injected via constructor parameters using domain interfaces
2. **Interface Segregation**: Small, focused interfaces in `domain/` package
3. **Single Responsibility**: Each package/file has one well-defined purpose
4. **DRY (Don't Repeat Yourself)**: Shared logic extracted to services and utilities
5. **Testability**: Comprehensive mocks in `mocks/` package for unit testing
6. **Builder Pattern**: Clean object construction for complex types (IPTables, IPRule)

### Dependency Injection Pattern

The codebase uses **AppDependencies** - a dependency injection container pattern:

**AppDependencies Container** (Implemented):
- Located in `domain/container.go` - central DI container for all dependencies
- Factory methods: `NewAppDependencies(config)`, `NewDefaultDependencies()`, `NewTestDependencies()`
- Provides access to: `KeeneticClient()`, `NetworkManager()`, `IPSetManager()`
- Benefits:
  - Configuration-driven setup (custom Keenetic URLs, disable Keenetic entirely)
  - Easy testing with mock implementations via `NewTestDependencies()`
  - Explicit dependency management without global state in commands
  - Single source of truth for dependency wiring

**Usage in Commands**:
```go
func (c *ApplyCommand) Run() error {
    // Create DI container with default config
    deps := domain.NewDefaultDependencies()

    // Create services from managed dependencies
    ipsetService := service.NewIPSetService(deps.IPSetManager())
    routingService := service.NewRoutingService(deps.NetworkManager(), deps.IPSetManager())

    // Use services...
}
```

**Configuration Options** (via `AppConfig`):
- `KeeneticURL`: Custom Keenetic RCI endpoint (default: `http://localhost:79/rci`)
- `DisableKeenetic`: Disable Keenetic API integration for non-router environments

**Evolution**:
Previously used `keenetic.GetDefaultClient()` with global state. Now uses `AppDependencies` for cleaner architecture. Legacy code still uses `GetDefaultClient()` but new code should use the container pattern.

See:
- `src/internal/domain/container.go` - AppDependencies implementation
- `src/internal/keenetic/client.go:39-59` - NewClientWithBaseURL for custom URLs
- `src/internal/commands/apply.go:70-76` - Example usage in commands

---

## Module Documentation

### Commands Layer (`commands/`)

**Purpose**: CLI command handlers that parse arguments and delegate to services.

**Key Components**:
- `Runner` interface: Unified command interface (Init, Run, Name)
- `AppContext`: Global application context (config path, verbosity, interfaces)
- Command implementations: Apply, Download, Undo, Service, Self-Check, etc.

**Pattern**:
```go
type ApplyCommand struct {
    fs  *flag.FlagSet
    cfg *config.Config
    // flags...
}

func (c *ApplyCommand) Init(args []string, ctx *AppContext) error {
    // Parse flags, load config, validate
}

func (c *ApplyCommand) Run() error {
    // Create DI container with default config
    deps := domain.NewDefaultDependencies()

    // Create services from managed dependencies
    ipsetService := service.NewIPSetService(deps.IPSetManager())
    routingService := service.NewRoutingService(deps.NetworkManager(), deps.IPSetManager())

    // Orchestrate via services
    return routingService.Apply(cfg, opts)
}
```

---

### Service Layer (`service/`)

**Purpose**: Business logic orchestration layer between commands and domain logic.

**Components**:

1. **RoutingService**: Orchestrates routing configuration
   - `Apply()`: Full routing setup (persistent + dynamic)
   - `ApplyPersistentOnly()`: Only iptables/ip rules
   - `UpdateRouting()`: Update routes for single interface
   - `Undo()`: Remove all routing configuration

2. **IPSetService**: Orchestrates ipset operations
   - `EnsureIPSetsExist()`: Create ipsets if missing
   - `PopulateIPSets()`: Import IPs from lists
   - `DownloadLists()`: Download remote IP lists

3. **ValidationService**: Centralized configuration validation
   - `ValidateConfig()`: Full config validation
   - Composable validators for different aspects
   - Clear, actionable error messages

**Benefits**:
- Commands stay thin (10-50 lines)
- Business logic reusable across commands
- Easier to test with mocks
- Single source of truth for orchestration

---

### Networking Layer (`networking/`)

**Purpose**: Core network configuration management for policy-based routing.

**Components**:

1. **Manager**: Main facade orchestrating all network operations
   - `ApplyPersistentConfig()`: iptables + ip rules
   - `ApplyRoutingConfig()`: ip routes
   - `UpdateRouting()`: Dynamic route updates
   - `UndoConfiguration()`: Clean removal

2. **PersistentConfigManager**: Manages iptables rules and ip rules
   - Rules stay active regardless of interface state
   - Traffic blocking when VPN down (security)

3. **RoutingConfigManager**: Manages dynamic ip routes
   - Adapts to interface up/down events
   - Selects best available interface

4. **InterfaceSelector**: Intelligent interface selection
   - Integrates with Keenetic API for connection status
   - Falls back to system interfaces if API unavailable
   - Prefers connected interfaces

5. **IPSetManager**: Implements `domain.IPSetManager` interface
   - Create ipsets with correct IP family
   - Flush and populate ipsets
   - Bulk import for performance

6. **Builders** (NEW - Phase 9):
   - `IPTablesBuilder`: Clean IPTables rule construction
   - `IPRuleBuilder`: Clean IP rule construction
   - Validation at build time

**Linux Kernel Integration**:
- **ipset**: Efficient IP matching (O(1) lookup, thousands of IPs)
- **iptables**: Packet marking with fwmark
- **ip rule**: Policy routing (fwmark → routing table)
- **ip route**: Custom routing tables
- **netlink API**: Go bindings via `vishvananda/netlink`

---

### Keenetic Integration (`keenetic/`)

**Purpose**: Client library for Keenetic Router RCI API.

**Features**:
- Interface detection (legacy and modern endpoints)
- DNS configuration retrieval
- Version detection
- Response caching for performance
- Implements `domain.KeeneticClient` interface

**Adapters**:
- **Legacy**: `/ip/hotspot/host` endpoint (deprecated)
- **Modern**: `/system` endpoint with system-name filtering
- Automatic detection of router capabilities

**Example**:
```go
client := keenetic.NewClient(nil)
interfaces, err := client.GetInterfaces()
// Returns map[string]Interface with up/connected status
```

---

### Domain Interfaces (`domain/`)

**Purpose**: Core abstractions for dependency injection, testing, and dependency management.

**Key Components**:

1. **Interfaces** (`interfaces.go`):
   - `NetworkManager`: Facade for all network operations
   - `RouteManager`: IP route add/delete/list operations
   - `InterfaceProvider`: Interface information retrieval
   - `IPSetManager`: IPSet create/flush/import operations
   - `KeeneticClient`: Router API interaction

2. **AppDependencies Container** (`container.go`):
   - Central DI container for managing all application dependencies
   - Factory methods for production and test configurations
   - Provides access to all managers and clients
   - Supports configuration-driven setup (custom URLs, disable features)

**Factory Methods**:
- `NewAppDependencies(cfg AppConfig)`: Create with custom configuration
- `NewDefaultDependencies()`: Create with default settings
- `NewTestDependencies(...)`: Create with mock implementations for testing

**Benefits**:
- Enables mocking for unit tests
- Loose coupling between layers
- Clear contracts between components
- Configuration-driven dependency creation
- Single source of truth for dependency wiring
- Easier to swap implementations

---

### Mocks Package (`mocks/`)

**Purpose**: Test doubles for comprehensive unit testing.

**Components**:
- `MockNetworkManager`: Configurable network operation mocks
- `MockRouteManager`: Route operation verification
- `MockInterfaceProvider`: Interface data stubbing
- `MockIPSetManager`: IPSet operation tracking
- `MockKeeneticClient`: Router API simulation

**Usage**:
```go
func TestServiceOperation(t *testing.T) {
    mockNet := &mocks.MockNetworkManager{}
    mockNet.ApplyPersistentConfigFunc = func(*config.Config) error {
        return nil
    }

    svc := service.NewRoutingService(mockNet, nil)
    err := svc.Apply(cfg, opts)
    // Verify mock interactions
}
```

---

### Lists Management (`lists/`)

**Purpose**: Download, parse, and import IP/domain lists.

**Features**:
- HTTP download with retry
- MD5 hash-based change detection
- Multiple sources: URL, file, inline
- DNS resolution for domains
- CIDR notation support
- Comment filtering

**Functions**:
- `DownloadLists()`: Download all remote lists
- `GetNetworksFromList()`: Extract IP networks
- `IterateOverList()`: Process each line with callback

---

### Configuration (`config/`)

**Purpose**: TOML configuration parsing and validation.

**Features**:
- Backward compatibility with deprecated fields
- Automatic field migration
- Type-safe structures
- Template variable support (iptables rules)
- Dual-stack IPv4/IPv6 support

**Structures**:
- `Config`: Root configuration
- `General`: Global settings
- `ListSource`: IP/domain list definition
- `IPSetConfig`: IPSet with routing configuration
- `RoutingConfig`: Interfaces, tables, rules, fwmark
- `IPTablesRule`: iptables rule template

---

## How It Works

### 1. Configuration

Users create a TOML configuration file at `/opt/etc/keen-pbr/keen-pbr.conf`:

```toml
[general]
lists_output_dir = "/opt/etc/keen-pbr/lists.d"
keenetic_url = "http://192.168.1.1"

[[lists]]
list_name = "vpn_ips"
url = "https://example.com/vpn-ips.txt"

[[ipsets]]
ipset_name = "vpn_routes"
ip_version = 4
list = "vpn_ips"

[ipsets.routing]
interfaces = ["wg0", "nwg0"]
ip_route_table = 100
ip_rule_priority = 100
fwmark = 100

[[ipsets.iptables_rules]]
table = "mangle"
chain = "PREROUTING"
rule = ["-m", "set", "--match-set", "{{ipset_name}}", "dst",
        "-j", "MARK", "--set-mark", "{{fwmark}}"]
```

### 2. List Management (Download Command)

```
┌─────────────┐
│   Command   │ keen-pbr download
└──────┬──────┘
       │
       ▼
┌─────────────────┐
│ IPSetService    │ DownloadLists(cfg)
└────────┬────────┘
         │
         ▼
┌──────────────────┐
│ lists.Downloader │ HTTP GET, MD5 check, file write
└──────────────────┘
```

**Process**:
1. Read configuration
2. For each list with URL:
   - Download via HTTP
   - Calculate MD5 hash
   - Compare with existing file hash
   - Write only if changed
3. Store in `/opt/etc/keen-pbr/lists.d/`

### 3. Routing Application (Apply Command)

```
┌──────────────┐
│   Command    │ keen-pbr apply
└──────┬───────┘
       │
       ▼
┌────────────────────┐    ┌──────────────────┐
│ RoutingService     │───>│ IPSetService     │ Create/populate ipsets
└──────┬─────────────┘    └──────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│          NetworkManager                      │
├──────────────────────┬───────────────────────┤
│ PersistentConfigMgr  │  RoutingConfigMgr     │
│ - iptables rules     │  - ip routes          │
│ - ip rules           │  - interface selection│
└──────────────────────┴───────────────────────┘
```

**Process**:
1. **Validate**: Configuration correctness
2. **Create IPSets**: `ipset create <name> hash:net family inet`
3. **Populate IPSets**: Import IPs from lists
4. **Apply IPTables**: Mark packets matching ipsets
5. **Apply IP Rules**: Route marked packets to table
6. **Apply IP Routes**: Add routes in table pointing to interface

**Example Kernel Configuration**:
```bash
# IPSet
ipset create vpn_routes hash:net family inet

# Import IPs
ipset add vpn_routes 1.1.1.0/24
ipset add vpn_routes 8.8.8.0/24

# IPTables (mark packets)
iptables -t mangle -A PREROUTING \
  -m set --match-set vpn_routes dst \
  -j MARK --set-mark 100

# IP Rule (fwmark → table)
ip rule add fwmark 100 table 100 priority 100

# IP Route (table → gateway)
ip route add default via 10.8.0.1 dev wg0 table 100
```

### 4. DNS-based Routing

Generate dnsmasq configuration:
```bash
keen-pbr print-dnsmasq-config > /opt/etc/dnsmasq.d/keen-pbr.conf
```

Output:
```
ipset=/example.com/vpn_routes
ipset=/another.com/vpn_routes
```

**How it works**:
1. dnsmasq resolves domain
2. Automatically adds resolved IP to ipset
3. Packet marked by iptables rule
4. Routed via ip rule + ip route

### 5. Service Mode (Daemon)

```bash
keen-pbr service
```

**Features**:
- Signal handling (SIGHUP: reload config)
- Interface monitoring
- Automatic route updates on interface up/down
- Graceful shutdown

**Integration with Keenetic NDM**:
- `/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh`
- Sends SIGHUP to daemon on interface changes
- Daemon updates routing automatically

---

## Build System

### Local Development

```bash
# Build binary
go build ./src/cmd/keen-pbr

# Run tests
go test ./...

# Run specific package tests
go test ./src/internal/service -v

# Cross-compile for router (MIPS little-endian)
GOOS=linux GOARCH=mipsle go build -o keen-pbr-mipsle ./src/cmd/keen-pbr
```

### Package Building

#### Local IPK Build
```bash
make packages    # Build all architectures
make mipsel      # Build for mipsel only
make repository  # Generate package index
```

**Outputs**: `out/<arch>/keen-pbr_<version>_<arch>.ipk`

#### Entware Build (CI)
- Uses OpenWrt build system
- Cross-compilation toolchain
- Architecture support: aarch64, mips, mipsel, x64, armv7

---

## CI/CD Workflows

### Build Workflow
**Trigger**: Every push to any branch

**Actions**:
1. Build IPK packages for all architectures
2. Upload artifacts to GitHub Actions

**Package naming**:
- Main: `keen-pbr-2.2.2-entware-aarch64-3.10.ipk`
- Branches: `keen-pbr-2.2.2-sha1a2b3c4-entware-aarch64-3.10.ipk`

### Release Workflow
**Trigger**: Push to `main` with `VERSION` file change

**Actions**:
1. Build packages
2. Create GitHub Release (draft)
3. Tag version (e.g., `v2.2.3`)
4. Deploy package repository to GitHub Pages
5. Upload IPK files

---

## Testing

### Test Coverage

The refactoring added comprehensive test coverage:

```bash
# Run all tests
go test ./...

# Run with coverage
go test -cover ./...

# Specific packages
go test ./src/internal/service -v
go test ./src/internal/networking -v
go test ./src/internal/keenetic -v
```

**Test Structure**:
- Unit tests with mocks for all services
- Integration tests for networking layer
- Table-driven tests for edge cases

**Mock Usage Example**:
```go
func TestApply(t *testing.T) {
    mockNet := &mocks.MockNetworkManager{
        ApplyPersistentConfigFunc: func(*config.Config) error {
            return nil
        },
    }

    svc := service.NewRoutingService(mockNet, nil)
    err := svc.Apply(cfg, opts)
    assert.NoError(t, err)
}
```

---

## CLI Commands

```bash
keen-pbr [options] <command>

Commands:
  apply                   # Apply routing configuration
  download                # Download remote IP/domain lists
  undo-routing            # Remove all routing configuration
  service                 # Run as daemon with auto-updates
  self-check              # Validate configuration
  print-dnsmasq-config    # Generate dnsmasq configuration
  interfaces              # List network interfaces
  dns                     # Show DNS proxy profile
  upgrade-config          # Upgrade config format

Options:
  -config string          # Config path (default: /opt/etc/keen-pbr/keen-pbr.conf)
  -verbose                # Enable debug logging
```

### Command Examples

```bash
# Download lists
keen-pbr -verbose download

# Apply with specific interface
keen-pbr apply --only-routing-for-interface wg0

# Service mode
keen-pbr service

# Validate configuration
keen-pbr self-check

# Generate dnsmasq config
keen-pbr print-dnsmasq-config > /opt/etc/dnsmasq.d/keen-pbr.conf
```

---

## Key Dependencies

```
github.com/coreos/go-iptables v0.8.0          # iptables management
github.com/pelletier/go-toml/v2 v2.2.3        # TOML config parsing
github.com/valyala/fasttemplate v1.2.2        # Template variables
github.com/vishvananda/netlink v1.3.0         # IP route/rule (netlink)
golang.org/x/sys v0.10.0                      # System calls
```

---

## Router Installation

### Via Entware Package Manager
```bash
opkg update
opkg install keen-pbr

# Configure
vi /opt/etc/keen-pbr/keen-pbr.conf

# Download lists
keen-pbr download

# Apply routing
keen-pbr apply

# Enable daemon
/opt/etc/init.d/S80keen-pbr start
```

### Manual Installation
```bash
# Download from releases
wget https://github.com/maksimkurb/keen-pbr/releases/download/v2.3.0/keen-pbr_2.3.0_mipsel.ipk

# Install
opkg install keen-pbr_*.ipk
```

---

## Project History

### Major Refactoring (November 2024)

The project underwent a comprehensive refactoring to improve modularity, testability, and maintainability:

**Completed Phases** (All 10 phases):
1. ✅ Foundation: Domain interfaces, errors, remove global state
2. ✅ Split Keenetic package into focused files
3. ✅ Refactor networking package
4. ✅ Commands refactored to use service layer
5. ✅ Domain-specific errors
6. ✅ Service layer implementation
7. ✅ Comprehensive mocks for testing
8. ✅ Package-level documentation
9. ✅ Builder patterns for complex objects
10. ✅ Validation service

**Improvements**:
- **Modularity**: Each package has single, well-defined responsibility
- **Testability**: Full dependency injection with mocks
- **Maintainability**: Service layer separates business logic
- **Documentation**: Package-level docs for all 12 packages
- **Code Quality**: Builder patterns, reduced duplication
- **Performance**: Optimized routing updates

**Metrics**:
- Build time: < 10s
- Test suite: < 5s
- All commits pass build and tests
- Nearly zero global state (1 backward-compat var)
- All dependencies injectable via interfaces

---

## Development Workflow

### Adding a New Command
1. Create `src/internal/commands/<name>.go`
2. Implement `Runner` interface
3. Use service layer for business logic
4. Register in `src/cmd/keen-pbr/main.go`
5. Add tests

### Adding Network Functionality
1. Define interface in `domain/interfaces.go` (if needed)
2. Implement in `networking/<feature>.go`
3. Add to facade (`Manager`)
4. Create mock in `mocks/`
5. Add tests with mocks

### Updating Configuration
1. Modify `config/types.go`
2. Update parser in `config/config.go`
3. Add validation in `service/validation_service.go`
4. Update example config

---

## Notes

- **Version Management**: VERSION file manually updated
- **No Auto-Versioning**: CI doesn't bump version
- **Cross-Compilation**: GOOS/GOARCH for router architectures
- **Target Platforms**: Keenetic routers (MIPS/ARM) with Entware
- **Router Integration**: NDM hooks in `/opt/etc/ndm/`
- **Documentation**: All packages documented with `go doc`
- **Architecture**: Clean layered design with DI
- **Testing**: Comprehensive mocks and unit tests

---

## Support

- **Issues**: GitHub Issues
- **Documentation**: README.md (Russian), README.en.md (English)
- **Configuration**: keen-pbr.example.conf
- **Community**: GitHub Discussions

---

*Last Updated: November 2024 - After complete refactoring (all 10 phases)*
