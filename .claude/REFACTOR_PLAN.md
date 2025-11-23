# Code Reduction & Unification Refactoring Plan

**Goals:**
1. Reduce codebase by ~700 lines by eliminating unnecessary abstractions
2. Unify APPLY and CHECK logic to prevent drift between features
3. Remove deprecated commands (`apply`, `undo-routing`)
4. Unify CLI commands with API endpoints to share same methods

**Principle:** Less code = fewer bugs. Single source of truth for all operations.

---

## Executive Summary

| Category | Lines to Remove | Risk |
|----------|-----------------|------|
| Unused Interfaces | ~50 | Low |
| Component Abstraction | ~400 | Medium |
| Builder Patterns | ~75 | Low |
| Service Layer Simplification | ~120 | Low |
| DI Complexity | ~40 | Low |
| Remove apply/undo commands | ~300 | Low |
| **Total** | **~985 lines** | |

---

## Phase 0: Unify Apply/Check Logic (HIGH PRIORITY)

### 0.1 Problem Statement

Currently there are TWO separate implementations:
1. **Applying** network config: `networking/manager.go`, `networking/persistent.go`, `networking/routing.go`
2. **Checking** network config: `commands/self_check.go`, `api/check.go`

These can easily drift apart when new features are added, causing:
- Self-check may report issues that don't match actual apply logic
- New components added to apply might not be checked
- Inconsistent behavior between CLI and API

### 0.2 Solution: Component-Based Architecture

**Already implemented in API** (`api/check.go`):
- Uses `NetworkingComponent` abstraction
- `ComponentBuilder.BuildComponents()` returns list of components for IPSet
- Each component has: `IsExists()`, `CreateIfNotExists()`, `DeleteIfExists()`, `ShouldExist()`

**Unified approach:**
1. **Apply** = iterate components and call `CreateIfNotExists()` / `DeleteIfExists()`
2. **Check** = iterate components and compare `IsExists()` vs `ShouldExist()`
3. **Undo** = iterate components and call `DeleteIfExists()`

### 0.3 Implementation Plan

**Step 1: Enhance ComponentBuilder**
```go
// networking/component_builder.go
type ComponentBuilder struct {
    keeneticClient *keenetic.Client
}

func (b *ComponentBuilder) BuildComponents(cfg *config.IPSetConfig) ([]NetworkingComponent, error) {
    // Returns: IPSet, IPRule, IPTables rules, IPRoutes (default + blackhole)
    // Each component knows if it "should exist" based on current interface state
}
```

**Step 2: Create unified ApplyService**
```go
// service/apply_service.go
type ApplyService struct {
    builder *networking.ComponentBuilder
}

func (s *ApplyService) Apply(cfg *config.Config) error {
    for _, ipset := range cfg.IPSets {
        components, _ := s.builder.BuildComponents(ipset)
        for _, comp := range components {
            if comp.ShouldExist() {
                comp.CreateIfNotExists()
            } else {
                comp.DeleteIfExists()
            }
        }
    }
}

func (s *ApplyService) Check(cfg *config.Config) []CheckResult {
    // Same BuildComponents call
    // Compare IsExists() vs ShouldExist()
}

func (s *ApplyService) Undo(cfg *config.Config) error {
    // Same BuildComponents call
    // Delete all components
}
```

**Step 3: Use unified service in:**
- `commands/service.go` (daemon mode) - calls `Apply()` internally
- `api/check.go` - calls `Check()`
- `commands/self_check.go` - calls `Check()` (same as API!)

### 0.4 Benefits
- Single source of truth for component building
- Apply and Check always stay in sync
- New features automatically get checking support
- CLI and API use exact same logic

---

## Phase 1: Remove Deprecated Commands (Low Risk)

### 1.1 Remove `keen-pbr apply` Command

**Reason:** The `service` command already handles apply operations when running.
Users should use the web UI or run `service` mode for applying.

**Files to modify:**
- `src/cmd/keen-pbr/main.go` - remove apply command registration
- Delete: `src/internal/commands/apply.go`

**Alternative workflow:**
```bash
# Instead of: keen-pbr apply
# Users should:
keen-pbr service  # runs as daemon with auto-apply on interface changes
# OR use web UI at http://router:8080/
```

### 1.2 Remove `keen-pbr undo-routing` Command

**Reason:** Undoing should be done via service stop or web UI.

**Files to modify:**
- `src/cmd/keen-pbr/main.go` - remove undo-routing command registration
- Delete: `src/internal/commands/undo.go`

**Alternative workflow:**
```bash
# Instead of: keen-pbr undo-routing
# Users should:
/opt/etc/init.d/S80keen-pbr stop  # stops service and cleans up
# OR use web UI service control
```

**Savings:** ~300 lines

---

## Phase 2: Unify CLI and API Methods

### 2.1 Unify `keen-pbr dns` with API

**Current CLI** (`commands/dns.go`):
- Creates new Keenetic client
- Calls `client.GetDNSServers()`
- Prints formatted output

**Current API** (`api/check.go` - split-dns check):
- Uses SSE streaming for DNS query monitoring
- Different functionality

**Solution:** Create shared DNS service

```go
// service/dns_service.go
type DNSService struct {
    client domain.KeeneticClient
}

// GetDNSServers returns DNS servers from Keenetic
func (s *DNSService) GetDNSServers() ([]keenetic.DNSServer, error) {
    return s.client.GetDNSServers()
}

// FormatDNSServers returns formatted string output
func (s *DNSService) FormatDNSServers(servers []keenetic.DNSServer) string {
    // Shared formatting logic
}
```

**CLI usage:**
```go
// commands/dns.go
func (c *DNSCommand) Run() error {
    dnsService := service.NewDNSService(c.deps.KeeneticClient())
    servers, err := dnsService.GetDNSServers()
    // Use FormatDNSServers for output
}
```

**Add API endpoint:**
```
GET /api/v1/dns-servers
```

### 2.2 Unify `keen-pbr interfaces` with API

**Current CLI** (`commands/interfaces.go`):
- Uses `networking.PrintInterfaces()`
- Gets interfaces from context
- Rich formatted output with Keenetic status

**Current API** (`api/interfaces.go`):
- Simple `net.Interfaces()` call
- Basic JSON response with name/up status
- No Keenetic integration

**Solution:** Create shared Interfaces service

```go
// service/interface_service.go
type InterfaceService struct {
    keeneticClient domain.KeeneticClient
}

type InterfaceInfo struct {
    Name        string `json:"name"`
    IsUp        bool   `json:"is_up"`
    IsConnected bool   `json:"is_connected"`  // from Keenetic API
    Type        string `json:"type"`          // e.g., "wireguard", "vpn"
    Description string `json:"description"`   // human-readable
}

func (s *InterfaceService) GetInterfaces() ([]InterfaceInfo, error) {
    // Merge net.Interfaces() with Keenetic API info
}
```

**Update CLI:**
```go
// commands/interfaces.go
func (g *InterfacesCommand) Run() error {
    svc := service.NewInterfaceService(g.deps.KeeneticClient())
    interfaces, _ := svc.GetInterfaces()
    // Use shared formatting
}
```

**Update API:**
```go
// api/interfaces.go
func (h *Handler) GetInterfaces(w http.ResponseWriter, r *http.Request) {
    svc := service.NewInterfaceService(h.deps.KeeneticClient())
    interfaces, _ := svc.GetInterfaces()
    writeJSONData(w, InterfacesResponse{Interfaces: interfaces})
}
```

---

## Phase 3: Remove Unused Code (Low Risk)

### 3.1 Delete Unused Interfaces

**File:** `src/internal/domain/interfaces.go`

Delete these interfaces (never implemented or referenced):

```go
// DELETE: RouteManager interface (lines ~76-95)
// DELETE: InterfaceProvider interface (lines ~97-107)
// DELETE: ConfigLoader interface (lines ~109-119)
```

**Verification:** `grep -r "RouteManager\|InterfaceProvider\|ConfigLoader" src/` shows no usage.

**Savings:** ~50 lines

---

## Phase 4: Eliminate Builder Pattern (Low Risk)

### 4.1 Inline IPTablesBuilder

**File:** `src/internal/networking/builders.go`

**Current (42 lines):**
```go
type IPTablesBuilder struct { ... }
func NewIPTablesBuilder() *IPTablesBuilder { ... }
func (b *IPTablesBuilder) Build() (*IPTablesRules, error) { ... }
```

**Replace with:** Direct function call in `component_builder.go`:
```go
func buildIPTablesRules(ipset *config.IPSetConfig, ipVersion int) (*IPTablesRules, error)
```

### 4.2 Inline IPRuleBuilder

**Current (33 lines):**
```go
type IPRuleBuilder struct { ... }
func NewIPRuleBuilder() *IPRuleBuilder { ... }
func (b *IPRuleBuilder) Build() (*IPRule, error) { ... }
```

**Replace with:** Direct function call.

**Savings:** ~75 lines

---

## Phase 5: Simplify Component Abstraction (Medium Risk)

This is the biggest win but requires careful refactoring.

### 5.1 Current Problem

```
6 files, ~570 lines for a simple CRUD pattern:
├── component.go (interface)
├── component_base.go (base type)
├── component_ipset.go
├── component_iprule.go
├── component_iptables.go
└── component_iproute.go
```

Each component just wraps: `IsExists()`, `CreateIfNotExists()`, `DeleteIfExists()`

### 5.2 Simplification Strategy

**Option A: Keep thin interface, remove base class**

Replace 6 files with inline implementations in `manager.go`:

```go
// Instead of:
for _, component := range components {
    if err := component.CreateIfNotExists(); err != nil { ... }
}

// Use direct calls:
if err := ipset.CreateIfNotExists(name, family); err != nil { ... }
if err := iprule.AddIfNotExists(rule); err != nil { ... }
if err := iptables.AppendUnique(table, chain, rule); err != nil { ... }
```

**Option B: Simplify to single file**

Merge all components into `components.go` (~150 lines total):

```go
type NetworkComponent interface {
    Apply() error
    Remove() error
    Exists() bool
}

func NewIPSetComponent(cfg *config.IPSetConfig) NetworkComponent { ... }
func NewIPRuleComponent(cfg *config.IPSetConfig) NetworkComponent { ... }
// ... etc
```

**Recommendation:** Option B - keeps component pattern but removes unnecessary indirection.

### 5.3 Files to Delete

- `src/internal/networking/component.go`
- `src/internal/networking/component_base.go`
- `src/internal/networking/component_ipset.go`
- `src/internal/networking/component_iprule.go`
- `src/internal/networking/component_iptables.go`
- `src/internal/networking/component_iproute.go`

**Files to Create:**
- `src/internal/networking/components.go` (~150 lines)

**Savings:** ~400 lines

---

## Phase 6: Simplify Service Layer (Low Risk)

### 6.1 Merge RoutingService into ApplyService

**File:** `src/internal/service/routing_service.go` (137 lines)

**After Phase 0**, RoutingService becomes redundant:
- `Apply()` → use unified ApplyService
- `Undo()` → use unified ApplyService

**Delete:** `src/internal/service/routing_service.go`

**Savings:** ~100 lines

### 6.2 Simplify IPSetService

**File:** `src/internal/service/ipset_service.go`

Keep but simplify `getNetworksForIPSet()`:

```go
// Current: O(n*m) nested loops
for _, listName := range ipset.Lists {
    for _, list := range cfg.Lists {
        if list.ListName == listName { ... }
    }
}

// Simplified: O(n) with map
listMap := make(map[string]*config.ListSource)
for _, list := range cfg.Lists {
    listMap[list.ListName] = list
}
for _, listName := range ipset.Lists {
    if list, ok := listMap[listName]; ok { ... }
}
```

**Savings:** ~20 lines

---

## Phase 7: Simplify Dependency Injection (Low Risk)

### 7.1 Remove IPSetManager Interface

**Current:**
```go
// domain/interfaces.go
type IPSetManager interface {
    Create(name string, family int) error
    Flush(name string) error
    Import(name string, networks []*net.IPNet) error
}

// networking/ipset_manager.go
type IPSetManagerImpl struct { ... }
```

**Problem:** Only one implementation exists. Interface adds indirection without benefit.

**Solution:** Use concrete type directly:
```go
// In domain/container.go
func (d *AppDependencies) IPSetManager() *networking.IPSetManagerImpl
```

**Savings:** ~40 lines (interface definition + wrapper methods)

### 7.2 Remove Type Casting Hack

**Current (container.go):**
```go
var concreteClient *keenetic.Client
if keeneticClient != nil {
    concreteClient = keeneticClient.(*keenetic.Client)  // Type casting!
}
```

**Solution:** Remove KeeneticClient interface, use `*keenetic.Client` directly.

**Savings:** ~10 lines

---

## Phase 8: Consolidate Validation (Low Risk)

### 8.1 Merge Validation Logic

**Current:**
- `config/config.go` → `ValidateConfig()` (structure validation)
- `service/validation_service.go` → `ValidateConfig()` (business validation)

**Solution:** Move ValidationService logic into config package:

```go
// config/validator.go (already exists, extend it)
func (c *Config) Validate() error {
    if err := c.validateStructure(); err != nil { return err }
    if err := c.validateIPSets(); err != nil { return err }
    if err := c.validateLists(); err != nil { return err }
    return nil
}
```

**Delete:** Most of `service/validation_service.go` (keep test file)

**Savings:** ~50 lines

---

## Implementation Order

```
Week 1 - High Priority Unification:
├── Phase 0: Unify Apply/Check logic (8 hours)
│   ├── Enhance ComponentBuilder with ShouldExist logic
│   ├── Create unified ApplyService
│   └── Update api/check.go and commands/self_check.go
│
├── Phase 2: Unify CLI/API (4 hours)
│   ├── Create DNSService (1 hour)
│   ├── Create InterfaceService (2 hours)
│   └── Update CLI commands and API endpoints (1 hour)

Week 2 - Command Removal:
├── Phase 1: Remove deprecated commands (2 hours)
│   ├── Delete apply.go
│   ├── Delete undo.go
│   └── Update main.go

Week 3 - Code Reduction:
├── Phase 3: Delete unused interfaces (1 hour)
├── Phase 4: Inline builders (2 hours)
├── Phase 6: Simplify IPSetService (1 hour)

Week 4 - Larger Refactors:
├── Phase 7: Simplify DI (4 hours)
├── Phase 8: Consolidate validation (2 hours)
├── Phase 5: Simplify Component abstraction (8-12 hours)
    ├── Create unified components.go
    ├── Update all usages
    └── Delete old component files
```

---

## Risk Mitigation

### Before Each Phase:
1. Run `make test` - ensure all tests pass
2. Create git branch for phase
3. Make incremental commits

### High-Risk Areas:
- **Phase 0 (Unify Apply/Check)** - Core functionality change
  - Mitigation: Extensive testing with real router
  - Keep old implementation until new one is proven

- **Phase 5 (Component abstraction removal)** - Used in manager.go and check.go
  - Mitigation: Keep NetworkingComponent interface temporarily, remove implementations one by one

### Rollback Plan:
- Each phase is independent
- Git branches allow easy rollback
- Tests catch regressions

---

## Success Metrics

| Metric | Before | After | Target |
|--------|--------|-------|--------|
| Total Lines | ~19,500 | ~18,500 | -1000 |
| Files in networking/ | 20+ | 15 | -5 |
| CLI Commands | 8 | 6 | -2 |
| Duplicate logic paths | 2 | 1 | -1 |
| Interfaces in domain/ | 6 | 3 | -3 |

---

## What NOT to Change

Keep these well-designed patterns:

1. **NetworkManager facade** - Good orchestration
2. **PersistentConfigManager / RoutingConfigManager** - Clean separation
3. **InterfaceSelector** - Smart interface selection
4. **ConfigHasher** - Useful for change detection
5. **RestartableRunner** - Clean crash isolation
6. **DNS Proxy architecture** - Well-designed async handling

---

## Code Smell Checklist

After refactoring, verify:

- [ ] No interface has single implementation
- [ ] No wrapper just delegates to another type
- [ ] No builder that could be a function
- [ ] No service that just calls one manager method
- [ ] No type casting between interface and concrete type
- [ ] No duplicate validation logic
- [ ] CLI and API share same business logic for same operations
- [ ] Apply and Check use same component building logic
