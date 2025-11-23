# Code Reduction Refactoring Plan

**Goal:** Reduce codebase by ~700 lines by eliminating unnecessary abstractions.
**Principle:** Less code = fewer bugs. Remove indirection that doesn't add value.

---

## Executive Summary

| Category | Lines to Remove | Risk |
|----------|-----------------|------|
| Unused Interfaces | ~50 | Low |
| Component Abstraction | ~400 | Medium |
| Builder Patterns | ~75 | Low |
| Service Layer Simplification | ~120 | Low |
| DI Complexity | ~40 | Low |
| **Total** | **~685 lines** | |

---

## Phase 1: Remove Unused Code (Low Risk)

### 1.1 Delete Unused Interfaces

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

## Phase 2: Eliminate Builder Pattern (Low Risk)

### 2.1 Inline IPTablesBuilder

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

### 2.2 Inline IPRuleBuilder

**Current (33 lines):**
```go
type IPRuleBuilder struct { ... }
func NewIPRuleBuilder() *IPRuleBuilder { ... }
func (b *IPRuleBuilder) Build() (*IPRule, error) { ... }
```

**Replace with:** Direct function call.

**Savings:** ~75 lines

---

## Phase 3: Simplify Component Abstraction (Medium Risk)

This is the biggest win but requires careful refactoring.

### 3.1 Current Problem

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

### 3.2 Simplification Strategy

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

**Recommendation:** Option A for maximum reduction.

### 3.3 Files to Delete

- `src/internal/networking/component.go`
- `src/internal/networking/component_base.go`
- `src/internal/networking/component_ipset.go`
- `src/internal/networking/component_iprule.go`
- `src/internal/networking/component_iptables.go`
- `src/internal/networking/component_iproute.go`
- `src/internal/networking/component_builder.go`

**Files to Modify:**
- `src/internal/networking/manager.go` - Use primitives directly
- `src/internal/commands/self_check.go` - Iterate primitives directly

**Savings:** ~400 lines

---

## Phase 4: Simplify Service Layer (Low Risk)

### 4.1 Merge RoutingService into Commands

**File:** `src/internal/service/routing_service.go` (137 lines)

**Current:**
```go
// In commands/apply.go:
routingService := service.NewRoutingService(networkMgr, ipsetMgr)
routingService.Apply(cfg, opts)
```

**Analysis:** RoutingService.Apply() just calls:
1. `networkMgr.ApplyPersistentConfig()`
2. `networkMgr.ApplyRoutingConfig()`

**Solution:** Call NetworkManager directly from commands:
```go
// In commands/apply.go:
if err := networkMgr.ApplyPersistentConfig(cfg.IPSets); err != nil { ... }
if err := networkMgr.ApplyRoutingConfig(cfg.IPSets); err != nil { ... }
```

**Delete:** `src/internal/service/routing_service.go`

**Savings:** ~100 lines (after inlining interface filtering logic)

### 4.2 Simplify IPSetService

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

## Phase 5: Simplify Dependency Injection (Low Risk)

### 5.1 Remove IPSetManager Interface

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

### 5.2 Remove Type Casting Hack

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

## Phase 6: Consolidate Validation (Low Risk)

### 6.1 Merge Validation Logic

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
Week 1:
├── Phase 1: Delete unused interfaces (1 hour)
├── Phase 2: Inline builders (2 hours)
└── Phase 4.2: Simplify IPSetService (1 hour)

Week 2:
├── Phase 5: Simplify DI (4 hours)
├── Phase 4.1: Merge RoutingService (4 hours)
└── Phase 6: Consolidate validation (2 hours)

Week 3:
└── Phase 3: Eliminate Component abstraction (8-12 hours)
    ├── Refactor manager.go
    ├── Refactor self_check.go
    └── Delete component files
```

---

## Risk Mitigation

### Before Each Phase:
1. Run `make test` - ensure all tests pass
2. Create git branch for phase
3. Make incremental commits

### High-Risk Areas:
- **Component abstraction removal** - Used in manager.go and self_check.go
  - Mitigation: Keep NetworkingComponent interface temporarily, remove implementations one by one

### Rollback Plan:
- Each phase is independent
- Git branches allow easy rollback
- Tests catch regressions

---

## Success Metrics

| Metric | Before | After | Target |
|--------|--------|-------|--------|
| Total Lines | ~19,500 | ~18,800 | -700 |
| Files in networking/ | 20+ | 15 | -5 |
| Interfaces in domain/ | 6 | 3 | -3 |
| Service files | 3 | 2 | -1 |

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
