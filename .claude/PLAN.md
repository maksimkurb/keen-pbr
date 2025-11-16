# Keen-PBR Refactoring Plan

**Goal:** Transform keen-pbr into a well-structured, maintainable Go project following best practices: Modularity, DRY, KISS, and Testability.

---

## Current State Analysis

### Identified Issues

1. **God Files/Functions:**
   - `keenetic/rci.go` (437 lines, 17 functions) - handles version detection, interface mapping, DNS parsing, and HTTP calls
   - `networking/network.go` (313 lines, 13 functions) - mixes persistent and dynamic routing concerns
   - `commands/self_check.go` (229 lines) - monolithic validation logic

2. **DRY Violations:**
   - `undoIpset()` duplicated in `undo.go` and `service.go`
   - Repeated error handling and logging patterns across packages
   - Interface validation duplicated in multiple commands
   - Config loading/validation repeated in every command

3. **Modularity Issues:**
   - Keenetic client mixes interface management, DNS queries, and version detection
   - Commands mix business logic with CLI concerns
   - Network configuration doesn't separate concerns well

4. **Testability Problems:**
   - Global caches (`keeneticVersionCache`, `httpClient`)
   - Direct shell command execution without abstraction
   - Tight coupling between layers

---

## Refactoring Phases

### Phase 1: Extract Interfaces and Domain Models

**Goal:** Create clear interfaces for dependency injection and testing

**New file: `src/internal/domain/interfaces.go`**
```go
// Domain interfaces for dependency injection
type KeeneticClient interface {
    GetVersion() (*KeeneticVersion, error)
    GetInterfaces() (map[string]Interface, error)
    GetDNSServers() ([]DNSServerInfo, error)
}

type NetworkManager interface {
    ApplyPersistentConfig(ipsets []*IPSetConfig) error
    ApplyRoutingConfig(ipsets []*IPSetConfig) error
    UndoConfig(ipsets []*IPSetConfig) error
}

type IPSetManager interface {
    Create(name string, family IpFamily) error
    Flush(name string) error
    Import(config *IPSetConfig, networks []netip.Prefix) error
}

type RouteManager interface {
    AddRoute(route Route) error
    DelRoute(route Route) error
    ListRoutes(table int) ([]Route, error)
}
```

**Benefits:**
- Enables dependency injection
- Makes testing trivial with mocks
- Decouples implementation from interface

---

### Phase 2: Refactor Keenetic Package

**Goal:** Split the god file into focused modules

**New structure:**
```
src/internal/keenetic/
├── client.go           # Main client implementation
├── version.go          # Version detection logic
├── interfaces.go       # Interface management
├── dns.go             # DNS-related functions
├── http.go            # HTTP transport layer
└── common.go          # Shared types (existing)
```

**`client.go`:**
```go
type Client struct {
    httpClient HTTPClient
    baseURL    string
    cache      *Cache  // Extracted cache management
}

func NewClient(httpClient HTTPClient) *Client {
    return &Client{
        httpClient: httpClient,
        baseURL:    rciPrefix,
        cache:      NewCache(),
    }
}

func (c *Client) GetVersion() (*KeeneticVersion, error)
func (c *Client) GetInterfaces() (map[string]Interface, error)
func (c *Client) GetDNSServers() ([]DNSServerInfo, error)
```

**`version.go`:**
```go
type VersionDetector struct {
    fetcher Fetcher
}

func (v *VersionDetector) Detect() (*KeeneticVersion, error)
func (v *VersionDetector) SupportsSystemNameEndpoint(version *KeeneticVersion) bool
```

**`interfaces.go`:**
```go
type InterfaceMapper struct {
    fetcher         Fetcher
    versionDetector *VersionDetector
}

func (m *InterfaceMapper) MapBySystemName() (map[string]Interface, error)
func (m *InterfaceMapper) mapModern(interfaces map[string]Interface) (map[string]Interface, error)
func (m *InterfaceMapper) mapLegacy(interfaces map[string]Interface) (map[string]Interface, error)
```

**`dns.go`:**
```go
type DNSClient struct {
    fetcher Fetcher
}

func (d *DNSClient) GetServers() ([]DNSServerInfo, error)
func (d *DNSClient) ParseProxyConfig(config string) []DNSServerInfo
```

**Benefits:**
- Single Responsibility Principle
- Each file under 150 lines
- Easy to test in isolation
- Clear separation of concerns

---

### Phase 3: Refactor Networking Package

**Goal:** Separate routing concerns and improve modularity

**New structure:**
```
src/internal/networking/
├── manager.go          # Main network manager
├── persistent.go       # Persistent configuration (iptables, ip rules)
├── routing.go          # Dynamic routing (ip routes)
├── interface_selector.go  # Interface selection logic
├── ipset.go           # IPSet operations (existing, refactored)
├── iptables.go        # IPTables operations (existing, refactored)
├── iproute.go         # IP route operations (existing)
├── iprule.go          # IP rule operations (existing)
└── interfaces.go      # Interface utilities (existing)
```

**`manager.go`:**
```go
type Manager struct {
    persistentConfig *PersistentConfigManager
    routingConfig    *RoutingConfigManager
    interfaceSelector *InterfaceSelector
}

func NewManager(keeneticClient KeeneticClient) *Manager

func (m *Manager) ApplyPersistentConfig(ipsets []*config.IPSetConfig) error
func (m *Manager) ApplyRoutingConfig(ipsets []*config.IPSetConfig) error
func (m *Manager) UndoConfig(ipsets []*config.IPSetConfig) error
```

**`persistent.go`:**
```go
type PersistentConfigManager struct {
    iptablesBuilder *IPTablesBuilder
    ipruleBuilder   *IPRuleBuilder
}

func (p *PersistentConfigManager) Apply(ipset *config.IPSetConfig) error
func (p *PersistentConfigManager) Remove(ipset *config.IPSetConfig) error
```

**`routing.go`:**
```go
type RoutingConfigManager struct {
    routeManager      RouteManager
    interfaceSelector *InterfaceSelector
}

func (r *RoutingConfigManager) Apply(ipset *config.IPSetConfig) error
func (r *RoutingConfigManager) Update(ipset *config.IPSetConfig) error
```

**`interface_selector.go`:**
```go
type InterfaceSelector struct {
    keeneticClient KeeneticClient
}

func (s *InterfaceSelector) ChooseBest(interfaces []string) (*Interface, error)
func (s *InterfaceSelector) IsUsable(iface *Interface) bool
```

**Benefits:**
- Clear separation between persistent and dynamic config
- Interface selection is now a standalone, testable component
- Manager acts as a facade, simplifying usage

---

### Phase 4: Refactor Commands Package

**Goal:** Extract common command logic and reduce duplication

**New structure:**
```
src/internal/commands/
├── base.go            # Base command with common functionality
├── service.go         # Service command (refactored)
├── apply.go          # Apply command (refactored)
├── download.go       # Download command
├── undo.go           # Undo command (refactored)
├── self_check/       # Self-check split into multiple files
│   ├── checker.go
│   ├── ipset_checker.go
│   ├── network_checker.go
│   └── config_checker.go
└── ...
```

**`base.go`:**
```go
type BaseCommand struct {
    fs     *flag.FlagSet
    ctx    *AppContext
    config *config.Config
}

func (b *BaseCommand) LoadConfig() error
func (b *BaseCommand) ValidateInterfaces() error
func (b *BaseCommand) Name() string
```

**Refactored commands inherit from BaseCommand:**
```go
type ApplyCommand struct {
    BaseCommand
    networkManager NetworkManager
    ipsetManager   IPSetManager

    // Command-specific flags
    SkipIpset    bool
    SkipRouting  bool
}

func (a *ApplyCommand) Init(args []string, ctx *AppContext) error {
    a.ctx = ctx
    a.fs.Parse(args)
    return a.LoadConfig() // Inherited method
}

func (a *ApplyCommand) Run() error {
    // Simplified logic using injected managers
}
```

**Benefits:**
- DRY: Config loading only in one place
- Each command focuses on business logic only
- Easy to test with mock managers

---

### Phase 5: Improve Error Handling

**Goal:** Create consistent, domain-specific errors

**New file: `src/internal/errors/errors.go`**
```go
type ErrorCode string

const (
    ErrCodeConfig     ErrorCode = "CONFIG_ERROR"
    ErrCodeNetwork    ErrorCode = "NETWORK_ERROR"
    ErrCodeKeenetic   ErrorCode = "KEENETIC_ERROR"
    ErrCodeIPSet      ErrorCode = "IPSET_ERROR"
)

type Error struct {
    Code    ErrorCode
    Message string
    Cause   error
}

func (e *Error) Error() string
func (e *Error) Unwrap() error

// Helper constructors
func NewConfigError(msg string, cause error) *Error
func NewNetworkError(msg string, cause error) *Error
```

**Benefits:**
- Consistent error handling across packages
- Better error messages for users
- Easier error testing

---

### Phase 6: Add Service Layer

**Goal:** Centralize business logic in service layer

**New structure:**
```
src/internal/service/
├── routing_service.go     # Orchestrates routing operations
├── ipset_service.go       # Orchestrates ipset operations
└── validation_service.go  # Centralized validation
```

**`routing_service.go`:**
```go
type RoutingService struct {
    networkManager NetworkManager
    ipsetManager   IPSetManager
    validator      *ValidationService
}

func (s *RoutingService) Apply(config *config.Config, opts ApplyOptions) error {
    // Validation
    if err := s.validator.ValidateConfig(config); err != nil {
        return err
    }

    // Business logic
    if !opts.SkipIPSet {
        if err := s.ipsetManager.Import(config); err != nil {
            return err
        }
    }

    if !opts.SkipRouting {
        if err := s.networkManager.ApplyPersistentConfig(config.IPSets); err != nil {
            return err
        }
        if err := s.networkManager.ApplyRoutingConfig(config.IPSets); err != nil {
            return err
        }
    }

    return nil
}

func (s *RoutingService) Undo(config *config.Config) error
func (s *RoutingService) Update(config *config.Config) error
```

**Benefits:**
- Commands become thin controllers
- Business logic centralized and reusable
- Easy to test complex workflows

---

### Phase 7: Improve Testability

**Goal:** Remove global state and add comprehensive mocks

#### 7.1 Remove global state

**Before:**
```go
var httpClient HTTPClient = &defaultHTTPClient{}
var keeneticVersionCache *KeeneticVersion = nil
```

**After:**
```go
type Cache struct {
    mu      sync.RWMutex
    version *KeeneticVersion
    ttl     time.Duration
}

func (c *Cache) GetVersion() (*KeeneticVersion, bool)
func (c *Cache) SetVersion(v *KeeneticVersion)
func (c *Cache) Clear()
```

#### 7.2 Add mock implementations

**New file: `src/internal/mocks/keenetic.go`**
```go
type MockKeeneticClient struct {
    GetVersionFunc     func() (*KeeneticVersion, error)
    GetInterfacesFunc  func() (map[string]Interface, error)
    GetDNSServersFunc  func() ([]DNSServerInfo, error)
}

func (m *MockKeeneticClient) GetVersion() (*KeeneticVersion, error) {
    if m.GetVersionFunc != nil {
        return m.GetVersionFunc()
    }
    return &KeeneticVersion{Major: 4, Minor: 3}, nil
}
```

**Benefits:**
- No global state
- Easy to test with mocks
- Concurrent-safe

---

### Phase 8: Add Comprehensive Documentation

**Goal:** Thoroughly document all packages and exported symbols

#### 8.1 Package-level documentation

**Add to each package:**
```go
// Package keenetic provides a client for interacting with Keenetic Router RCI API.
//
// The client supports version detection, interface mapping, and DNS configuration retrieval.
//
// Example usage:
//
//	client := keenetic.NewClient(&defaultHTTPClient{})
//	interfaces, err := client.GetInterfaces()
//	if err != nil {
//	    log.Fatal(err)
//	}
package keenetic
```

#### 8.2 Function documentation

Ensure every exported function has:
- Purpose description
- Parameter descriptions
- Return value descriptions
- Example usage (for complex functions)

**Benefits:**
- Self-documenting code
- Better IDE support
- Easier onboarding

---

### Phase 9: Extract Utilities

**Goal:** Create builder patterns for complex objects

**New file: `src/internal/networking/builders.go`**
```go
type IPTablesBuilder struct {
    ipset    *config.IPSetConfig
    protocol iptables.Protocol
}

func NewIPTablesBuilder(ipset *config.IPSetConfig) *IPTablesBuilder
func (b *IPTablesBuilder) Build() (*IPTableRules, error)

type IPRuleBuilder struct {
    ipset *config.IPSetConfig
}

func NewIPRuleBuilder(ipset *config.IPSetConfig) *IPRuleBuilder
func (b *IPRuleBuilder) Build() *IpRule
```

**Benefits:**
- Cleaner object construction
- Validation at build time
- Immutable objects

---

### Phase 10: Refine Configuration

**Goal:** Centralized configuration validation

**New file: `src/internal/service/validation_service.go`**
```go
type ValidationService struct {
    interfaceValidator *InterfaceValidator
    ipsetValidator     *IPSetValidator
    routingValidator   *RoutingValidator
}

func (v *ValidationService) ValidateConfig(cfg *config.Config) error {
    validators := []func(*config.Config) error{
        v.validateIPSets,
        v.validateLists,
        v.validateRouting,
        v.validateInterfaces,
    }

    for _, validator := range validators {
        if err := validator(cfg); err != nil {
            return err
        }
    }
    return nil
}
```

**Benefits:**
- Single source of truth for validation
- Composable validators
- Clear error messages

---

## Migration Strategy

### Recommended Order

**Week 1-2: Foundation**
- ✅ Phase 1: Create domain interfaces
- ✅ Phase 5: Domain-specific errors
- ✅ Phase 7.1: Remove global state

**Week 3-4: Core Refactoring**
- Phase 2: Split Keenetic package
- Phase 7.2: Add mocks for Keenetic

**Week 5-6: Networking Layer**
- Phase 3: Refactor networking package
- Add mocks for networking

**Week 7-8: Service Layer**
- Phase 6: Create service layer
- Phase 4: Refactor commands

**Week 9-10: Polish**
- Phase 8: Documentation
- Phase 9: Extract utilities
- Phase 10: Validation service

---

## Expected Outcomes

**Modularity:**
- Each package has a single, well-defined responsibility
- Maximum file size: ~200 lines
- Maximum function size: ~50 lines

**DRY:**
- No code duplication
- Shared logic in base classes/services
- Common patterns extracted to utilities

**KISS:**
- Each function does one thing well
- Clear, readable code
- Minimal complexity

**Testability:**
- 80%+ test coverage achievable
- All external dependencies mockable
- Fast unit tests (no network/shell calls in tests)

**Documentation:**
- Every exported symbol documented
- Package-level documentation
- Examples for complex operations

---

## Success Metrics

- [ ] No file exceeds 200 lines (except tests)
- [ ] No function exceeds 50 lines
- [ ] Test coverage > 80%
- [ ] All exported symbols documented
- [ ] Zero global state
- [ ] All external dependencies injectable
- [ ] Build time < 10s
- [ ] Test suite runs in < 5s
