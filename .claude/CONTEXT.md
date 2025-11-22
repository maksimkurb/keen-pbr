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
│   ├── frontend/                     # React web UI (embedded in binary)
│   │   ├── src/                      # React source code
│   │   │   ├── App.tsx               # Main application
│   │   │   ├── api/client.ts         # API client
│   │   │   ├── hooks/                # React Query hooks
│   │   │   ├── pages/                # Page components
│   │   │   └── i18n/                 # Internationalization
│   │   ├── components/               # UI components
│   │   │   ├── ui/                   # shadcn/ui components
│   │   │   ├── lists/                # Lists page components
│   │   │   └── routing-rules/        # Routing rules components
│   │   ├── package.json              # NPM dependencies
│   │   └── embed.go                  # Embed dist/ into binary
│   │
│   └── internal/                     # Private application packages (not importable)
│       ├── api/                      # REST API server (chi router)
│       │   ├── router.go             # Route definitions
│       │   ├── handlers.go           # Handler struct
│       │   ├── lists.go              # Lists endpoints
│       │   ├── ipsets.go             # IPSets endpoints
│       │   ├── settings.go           # Settings endpoints
│       │   ├── status.go             # Status endpoints
│       │   ├── interfaces.go         # Interfaces endpoint
│       │   ├── service.go            # Service control and dnsmasq restart
│       │   ├── check.go              # Network diagnostics and health checks
│       │   ├── types.go              # API types
│       │   ├── middleware.go         # HTTP middleware
│       │   └── response.go           # Response helpers
│       │
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
│       │   ├── upgrade_config.go     # Configuration format upgrader
│       │   ├── server.go             # API server command
│       │   └── service_manager.go    # Service lifecycle management
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

---

## Web Interface & REST API

### Overview

In addition to the CLI, keen-pbr provides a web-based management interface with a REST API backend. The web UI is built with React 19 and integrates seamlessly with the daemon mode.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Web UI (React 19)                         │
│  Components: Lists, Routing Rules, Settings, Status         │
│  State Management: React Query (TanStack Query)             │
│  UI Framework: shadcn/ui components                         │
└──────────────────────┬──────────────────────────────────────┘
                       │ HTTP/JSON
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              REST API (internal/api/)                        │
│  Router: chi/v5 with middleware (auth, CORS, logging)       │
│  Endpoints: Lists, IPSets, Settings, Status, Interfaces     │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│          Backend Services (service/, networking/)            │
│  Same business logic layer used by CLI commands             │
└─────────────────────────────────────────────────────────────┘
```

### REST API Structure (`src/internal/api/`)

**Files**:
- `router.go`: HTTP router with middleware stack and route definitions
- `handlers.go`: Handler struct with dependency injection
- `lists.go`: Lists CRUD endpoints
- `ipsets.go`: IPSets (routing rules) CRUD endpoints
- `settings.go`: General settings endpoints
- `status.go`: Service status and version info
- `interfaces.go`: Network interfaces endpoint
- `service.go`: Service control (start/stop/restart) and dnsmasq control
- `check.go`: Network diagnostics (ping, traceroute, routing checks, split-dns)
- `types.go`: Request/response type definitions
- `middleware.go`: CORS, logging, error recovery, private subnet restrictions
- `response.go`: Standardized JSON response wrappers

**API Endpoints (v1)**:

```
Lists Management:
  GET    /api/v1/lists              # List all lists
  POST   /api/v1/lists              # Create new list
  GET    /api/v1/lists/{name}       # Get list details (includes inline hosts)
  PUT    /api/v1/lists/{name}       # Update list
  DELETE /api/v1/lists/{name}       # Delete list
  POST   /api/v1/lists-download     # Download all URL-based lists
  POST   /api/v1/lists-download/{name}  # Download specific list

IPSets (Routing Rules):
  GET    /api/v1/ipsets             # List all routing rules
  POST   /api/v1/ipsets             # Create routing rule
  GET    /api/v1/ipsets/{name}      # Get routing rule details
  PUT    /api/v1/ipsets/{name}      # Update routing rule
  DELETE /api/v1/ipsets/{name}      # Delete routing rule

Settings:
  GET    /api/v1/settings           # Get general settings
  PATCH  /api/v1/settings           # Update general settings

System:
  GET    /api/v1/interfaces         # List network interfaces
  GET    /api/v1/status             # Service status and version
  GET    /api/v1/health             # Health check
  POST   /api/v1/service            # Control service (start/stop/restart)
  POST   /api/v1/dnsmasq            # Restart dnsmasq

Network Diagnostics:
  POST   /api/v1/check/routing      # Check routing for host
  GET    /api/v1/check/ping         # SSE stream for ping
  GET    /api/v1/check/traceroute   # SSE stream for traceroute
  GET    /api/v1/check/self         # SSE stream for self-check (config validation, iptables, ip rules, ipsets)
  GET    /api/v1/check/split-dns    # SSE stream for split-DNS check (monitor DNS queries)
```

**JSON Field Naming**: All API responses use `snake_case` for field names (e.g., `list_name`, `ip_version`, `flush_before_applying`).

**Response Format**:
```json
{
  "data": {
    "lists": [...],
    "ipsets": [...]
  }
}
```

**Error Format**:
```json
{
  "error": {
    "code": "CONFIG_INVALID",
    "message": "List name already exists",
    "details": {}
  }
}
```

### Frontend Structure (`src/frontend/`)

**Technology Stack**:
- **React 19**: Latest React with concurrent features
- **TypeScript**: Full type safety
- **React Router v6**: Hash-based routing for static deployment
- **React Query (TanStack Query)**: Server state management with cache invalidation
- **shadcn/ui**: Modern component library (Button, Dialog, Input, Select, etc.)
- **Tailwind CSS**: Utility-first styling
- **react-i18next**: Internationalization (English + Russian)
- **sonner**: Toast notifications
- **Lucide React**: Icon library
- **Rsbuild**: Fast build tool (Rspack-based)

**Directory Structure**:
```
src/frontend/
├── src/
│   ├── App.tsx                    # Main app with routing
│   ├── index.tsx                  # Entry point
│   ├── api/
│   │   └── client.ts              # API client with typed methods
│   ├── hooks/
│   │   ├── useLists.ts            # Lists CRUD hooks
│   │   ├── useIPSets.ts           # IPSets CRUD hooks
│   │   ├── useSettings.ts         # Settings hooks
│   │   └── useInterfaces.ts       # Network interfaces hook
│   ├── i18n/
│   │   ├── config.ts              # i18n configuration
│   │   └── locales/
│   │       ├── en.json            # English translations
│   │       └── ru.json            # Russian translations
│   ├── pages/
│   │   ├── Lists.tsx              # Lists management page
│   │   ├── RoutingRules.tsx       # Routing rules page
│   │   ├── Settings.tsx           # General settings page
│   │   └── Status.tsx             # Service status page
│   └── lib/
│       └── utils.ts               # Utility functions
│
├── components/
│   ├── ui/                        # shadcn/ui components
│   │   ├── button.tsx
│   │   ├── dialog.tsx
│   │   ├── input.tsx
│   │   ├── select.tsx
│   │   ├── checkbox.tsx
│   │   ├── badge.tsx
│   │   ├── radio-group.tsx
│   │   ├── command.tsx            # Combobox command palette
│   │   ├── popover.tsx            # Popover container
│   │   └── ...
│   │
│   ├── lists/                     # Lists page components
│   │   ├── ListsTable.tsx         # Table with filters and actions
│   │   ├── ListFilters.tsx        # Search and type filters
│   │   ├── CreateListDialog.tsx   # Create list modal
│   │   └── EditListDialog.tsx     # Edit list modal
│   │
│   └── routing-rules/             # Routing rules components
│       ├── RoutingRulesTable.tsx  # Table with 7 columns
│       ├── RuleFilters.tsx        # Search, list, version filters
│       ├── CreateRuleDialog.tsx   # Multi-section create form
│       ├── EditRuleDialog.tsx     # Multi-section edit form
│       └── DeleteRuleConfirmation.tsx  # Delete confirmation dialog
│
├── package.json                   # Dependencies
├── rsbuild.config.ts              # Build configuration
├── tsconfig.json                  # TypeScript configuration
└── tailwind.config.ts             # Tailwind configuration
```

**Key Features**:

1. **Lists Management**:
   - CRUD operations for URL, file, and inline hosts lists
   - On-demand list downloads with MD5 change detection
   - Display last modified date for URL lists
   - Inline hosts editing (up to 1000 hosts)
   - Statistics: total hosts, IPv4/IPv6 subnet counts
   - Type and search filtering

2. **Routing Rules (IPSets)**:
   - Multi-section form with RadioGroup for IP version
   - Combobox for list selection with searchable dropdown
   - Combobox for interface selection (fetches live network interfaces)
   - Custom interface names supported (can type interfaces that don't exist)
   - Interface reordering with up/down arrow buttons to control priority
   - Lists displayed as unordered list (UL)
   - Interfaces displayed as ordered list (OL) showing priority
   - Routing configuration: priority, table, fwmark, DNS override
   - Advanced IPTables Rules section (collapsible accordion):
     - Supports multiple custom iptables rules
     - Chain, table, and rule arguments configuration
     - Template variable insertion dropdown ({{ipset_name}}, {{fwmark}}, {{table}}, {{priority}})
     - Pre-populated with default PREROUTING/mangle rule
     - Fully internationalized (English + Russian)
   - Options: flush_before_applying, kill_switch
   - Filters: search by name, filter by list, filter by IP version
   - URL state persistence for filters

3. **General Settings**:
   - Lists output directory configuration
   - Keenetic DNS integration toggle
   - Fallback DNS server

4. **Service Status**:
   - Version information (keen-pbr, Keenetic)
   - Service running status (keen-pbr, dnsmasq)
   - Service control (start/stop/restart)

**UI Patterns**:

- **Dialogs**: All create/edit forms use Dialog component with FieldGroup/Field structure
- **Toast Notifications**: Success (green), Info (blue), Error (red) for user feedback
- **Loading States**: Skeleton loaders and disabled states during operations
- **Empty States**: Context-aware empty state messages
- **Validation**: Client-side validation before API calls
- **Cache Invalidation**: React Query automatically refetches after mutations
- **Description Placement**: Field descriptions shown below inputs for "Cannot be changed" messages

**Build Output**:
- Total size: ~650 KB (gzipped: ~190 KB)
- Static files served from embedded filesystem
- Hash-based routing for direct URL access
- Automatic code splitting

### Integration with CLI

The web interface and CLI share the same backend services:

```go
// API Handler reuses service layer
func (h *Handler) CreateList(w http.ResponseWriter, r *http.Request) {
    // Same config loading as CLI
    cfg, err := h.loadConfig()

    // Same validation as CLI
    if err := cfg.ValidateConfig(); err != nil {
        WriteError(w, err)
        return
    }

    // Save and restart (same as CLI apply)
    h.saveConfig(cfg)
    h.serviceMgr.Restart()
}
```

**Benefits**:
- Single source of truth for business logic
- Consistent behavior between CLI and web
- CLI can be used for automation/scripting
- Web UI for user-friendly management
- Both modes use same configuration file

### Deployment

**Embedded Static Files**:
```go
// src/frontend/embed.go
//go:embed dist/*
var distFS embed.FS

func GetHTTPFileSystem() (http.FileSystem, error) {
    return http.FS(distFS), nil
}
```

**Router Integration**:
```go
// Serve static frontend files (catch-all route)
if staticFS, err := frontend.GetHTTPFileSystem(); err == nil {
    fileServer := http.FileServer(staticFS)
    r.Handle("/*", fileServer)
}
```

**Access**:
- Web UI: `http://<router-ip>:8080/`
- API: `http://<router-ip>:8080/api/v1/`
- Restricted to private subnet IPs (middleware)

### Frontend Development Guide

**Commands**:
- `npm run dev` - Start the dev server
- `npm run build` - Build the app for production
- `npm run preview` - Preview the production build locally

**Docs**:
- Rsbuild: https://rsbuild.rs/llms.txt
- Rspack: https://rspack.rs/llms.txt

**Tools**:
- **Biome**:
  - Run `npm run lint` to lint your code
  - Run `npm run format` to format your code

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

*Last Updated: November 2024 - After complete refactoring (all 10 phases), web interface addition, and advanced IPTables Rules UI*
