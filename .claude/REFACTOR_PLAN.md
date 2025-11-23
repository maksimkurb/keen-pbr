# Code Reduction & Unification Refactoring Plan

## Status: Mostly Completed

**Completed Phases:** 0, 1, 2, 3, 4, 6, 8
**Deferred Phases:** 5, 7

**Actual Results:**
| Category | Lines Changed | Status |
|----------|---------------|--------|
| Phase 0: Unify Apply/Check | ~100 removed | COMPLETED |
| Phase 1: Remove apply/undo commands | ~350 removed | COMPLETED |
| Phase 2: Unify CLI/API Methods | +320 (shared services) | COMPLETED |
| Phase 3: Remove unused interfaces | ~50 removed | COMPLETED |
| Phase 4: Eliminate Builder Pattern | ~40 removed | COMPLETED |
| Phase 6: Simplify Service Layer | ~100 removed | COMPLETED |
| Phase 8: Consolidate Validation | ~300 removed | COMPLETED |
| **Total** | **~940 lines removed** | |

---

## Completed Phases

### Phase 0: Unify Apply/Check Logic - COMPLETED

Updated `commands/self_check.go` to use the same `ComponentBuilder` pattern as `api/check.go`:
- Both CLI and API now use `networking.ComponentBuilder.BuildComponents()`
- Both check `component.IsExists()` vs `component.ShouldExist()`
- Consistent output formatting and messages

### Phase 1: Remove Deprecated Commands - COMPLETED

Deleted:
- `src/internal/commands/apply.go`
- `src/internal/commands/apply_test.go`
- `src/internal/commands/undo.go`

Updated:
- `src/cmd/keen-pbr/main.go` - removed command registrations and help text

Alternative workflow: Users should use `keen-pbr service` (daemon mode) or web UI.

### Phase 3: Remove Unused Interfaces - COMPLETED

Deleted from `src/internal/domain/interfaces.go`:
- `RouteManager` interface
- `InterfaceProvider` interface
- `ConfigLoader` interface

These were defined but never implemented or used.

### Phase 4: Eliminate Builder Pattern - COMPLETED

Converted builder pattern to simple functions in `networking/builders.go`:
- `NewIPTablesBuilder(cfg).Build()` → `BuildIPTablesRules(cfg)`
- `NewIPRuleBuilder(cfg).Build()` → `BuildIPRuleFromConfig(cfg)`

Updated all call sites in:
- `component_iprule.go`
- `component_iptables.go`
- `persistent.go`

### Phase 6: Simplify Service Layer - COMPLETED

Deleted:
- `src/internal/service/routing_service.go` - no longer used after removing apply command

IPSetService remains for download command functionality.

### Phase 8: Consolidate Validation - COMPLETED

Deleted:
- `src/internal/service/validation_service.go`
- `src/internal/service/validation_service_test.go`

Updated `commands/common.go` to use only `config.ValidateConfig()`.
Interface validation is handled separately by `networking.ValidateInterfacesArePresent()`.

---

### Phase 2: Unify CLI and API Methods - COMPLETED

Created shared services for DNS and interfaces:

- `service/dns_service.go` - DNSService with GetDNSServers() and FormatDNSServers()
- `service/interface_service.go` - InterfaceService with GetInterfaces() and FormatInterfacesForCLI()

Updated CLI commands:
- `commands/dns.go` uses DNSService
- `commands/interfaces.go` uses InterfaceService

Updated API:
- `api/interfaces.go` GetInterfaces() now includes Keenetic metadata
- New `GET /api/v1/dns-servers` endpoint added
- Shared `DNSServerInfo` type used across API

---

## Deferred Phases

### Phase 5: Simplify Component Abstraction - DEFERRED

**Reason:** After analysis, consolidating component files would NOT reduce code significantly:
- `component_dns_redirect.go` alone is 362 lines
- Total component files are 600+ lines of actual implementation logic
- Consolidating into single file would hurt readability without reducing code

Current component file structure is well-organized and maintainable.

### Phase 7: Simplify Dependency Injection - DEFERRED

**Reason:** The KeeneticClient interface IS used for mocking in tests:
- `dnsproxy/upstreams/keenetic_test.go` - uses `mockKeeneticClient`
- `config/hasher_deadlock_test.go` - uses `mockKeeneticClient`

Removing the interface would require changing test strategy.

The type casting hack in `container.go` exists because `ComponentBuilder` requires concrete `*keenetic.Client`.
Fixing this would require changing `InterfaceSelector` to accept the interface.

---

## What NOT to Change

These patterns are well-designed and should be kept:

1. **NetworkManager facade** - Good orchestration
2. **PersistentConfigManager / RoutingConfigManager** - Clean separation
3. **InterfaceSelector** - Smart interface selection
4. **ConfigHasher** - Useful for change detection
5. **RestartableRunner** - Clean crash isolation
6. **DNS Proxy architecture** - Well-designed async handling
7. **Component files separation** - Good maintainability

---

## Code Smell Checklist (Post-Refactor)

After refactoring, these issues remain:

- [x] No interface has single implementation - MOSTLY FIXED (IPSetManager still has interface for testing)
- [x] No wrapper just delegates to another type - FIXED
- [x] No builder that could be a function - FIXED
- [x] No service that just calls one manager method - FIXED
- [ ] No type casting between interface and concrete type - NOT FIXED (KeeneticClient)
- [x] No duplicate validation logic - FIXED
- [x] CLI and API share same business logic - FIXED (dns/interfaces now share services)
- [x] Apply and Check use same component building logic - FIXED
