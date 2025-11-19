# Network Component Abstraction Refactoring

## Context

Currently, the self-check API (`src/internal/api/check.go`) uses direct command execution (`exec.Command`) to check networking state:
- `ipset list <name>` - Check if IPSet exists
- `ip rule show` - Check IP rules
- `ip route show table X` - Check IP routes
- `iptables -C` / `ip6tables -C` - Check IPTables rules

This approach has several issues:
1. **Duplication**: Apply logic and check logic are separate implementations
2. **No reuse of managers**: We have `src/internal/networking/ipset_manager.go` but don't use it in API
3. **Hard to test**: Direct command execution makes unit testing difficult
4. **Inconsistency**: Different code paths for apply vs check can lead to divergence

## Goal

Create a unified `NetworkingComponent` abstraction that:
- Encapsulates all networking elements (IPSet, IPRule, IPRoute, IPTables)
- Provides consistent interface for CRUD operations
- Supports declarative "should exist" logic based on runtime state
- Used by both apply operations and self-check operations
- Leverages existing managers and libraries instead of direct commands

## Architecture

### Core Interface

```go
type NetworkingComponent interface {
    IsExists() (bool, error)        // Check current state
    ShouldExist() bool              // Determine expected state
    CreateIfNotExists() error       // Apply if needed
    DeleteIfExists() error          // Cleanup if needed
    GetType() ComponentType         // For categorization
    GetIPSetName() string           // For grouping
    GetDescription() string         // Human-readable explanation
    GetCommand() string             // CLI command for debugging
}
```

### Component Types

1. **IPSetComponent** - Wraps existing `IPSet` type from `networking/ipset.go`
2. **IPRuleComponent** - Wraps existing `IPRule` type from `networking/iprule.go`
3. **IPRouteComponent** - Wraps existing `Route` type from `networking/iproute.go`
4. **IPTablesRuleComponent** - Wraps existing `IPTablesRules` type from `networking/iptables.go`

### Key Design Decisions

1. **ShouldExist() Logic**:
   - IPSet: Always `true` for configured rules
   - IPRule: Always `true` for configured rules
   - IPRoute (default): `true` only if this is the best available interface
   - IPRoute (blackhole): `true` only if no interfaces are available
   - IPTables: Always `true` for configured rules

2. **ComponentBuilder**:
   - Generates all components for an IPSet configuration
   - Takes interface selector as dependency for route decisions
   - Returns slice of `NetworkingComponent` for uniform processing

3. **Apply Logic**:
   ```go
   for _, component := range components {
       if component.ShouldExist() {
           component.CreateIfNotExists()
       } else {
           component.DeleteIfExists()
       }
   }
   ```

4. **Check Logic**:
   ```go
   for _, component := range components {
       exists, _ := component.IsExists()
       shouldExist := component.ShouldExist()
       passed := (exists == shouldExist)
       // Report result
   }
   ```

## Implementation Plan

### Phase 1: Core Abstractions ✅
- [x] Create `src/internal/networking/component.go` with interface
- [x] Create `src/internal/networking/component_base.go` with common fields

### Phase 2: Concrete Components ✅
- [x] Create `src/internal/networking/component_ipset.go`
- [x] Create `src/internal/networking/component_iprule.go`
- [x] Create `src/internal/networking/component_iproute.go`
- [x] Create `src/internal/networking/component_iptables.go`

### Phase 3: Component Builder ✅
- [x] Create `src/internal/networking/component_builder.go`
- [x] Implement `BuildComponents(ipsetCfg)` method
- [x] Implement `BuildAllComponents(cfg)` method
- [x] Fix import cycle by accepting *keenetic.Client instead of domain interface

### Phase 4: Refactor Apply Logic ✅
- [x] Update `src/internal/networking/manager.go` - `ApplyPersistentConfig()`
- [x] Update `src/internal/networking/manager.go` - `ApplyRoutingConfig()`
- [x] Update `src/internal/networking/manager.go` - `UndoConfig()`
- [x] Implement stale route cleanup in ApplyRoutingConfig
- [x] Maintain backward compatibility with existing managers

### Phase 5: Refactor Self-Check ✅
- [x] Update `src/internal/api/check.go` - `checkIPSetSelfJSON()`
- [x] Update `src/internal/api/check.go` - `checkIPSetSelfSSE()`
- [x] Add `getComponentMessage()` helper for intelligent diagnostics
- [x] Remove `checkIPTablesRule()` and `checkIPTablesRuleJSON()`
- [x] Achieve identical logic between SSE and JSON modes

### Phase 6: Testing ✅
- [x] Add unit tests for each component type
- [x] Add integration tests for component builder
- [x] Fix ComponentBuilder to skip IPTables when no rules configured
- [x] Add graceful test skipping when iptables binary not available

### Phase 7: Cleanup (Optional)
- [x] Remove old command-based check helpers
- [ ] Optionally migrate CLI `self_check.go` to components
- [ ] Optionally remove unused PersistentConfigManager/RoutingConfigManager
- [ ] Add inline documentation for component usage

## Migration Strategy

1. **Additive First**: Add new component system alongside existing code
2. **Gradual Migration**: Update one consumer at a time (API, then apply, then CLI)
3. **Parallel Testing**: Both old and new paths should work during transition
4. **Final Cleanup**: Remove old code only after all consumers migrated

## Benefits

1. **Unified Logic**: Single source of truth for what should exist
2. **Testability**: Easy to mock components for unit tests
3. **Reusability**: Components used across CLI, API, and service layer
4. **Type Safety**: Compile-time guarantees instead of string manipulation
5. **Maintainability**: Changes to networking logic in one place
6. **Debugging**: Each component provides CLI command for manual inspection

## Files to Create

```
src/internal/networking/
├── component.go              # Interface definition (Phase 1)
├── component_base.go         # Common base struct (Phase 1)
├── component_ipset.go        # IPSet component (Phase 2)
├── component_iprule.go       # IPRule component (Phase 2)
├── component_iproute.go      # IPRoute component (Phase 2)
├── component_iptables.go     # IPTables component (Phase 2)
├── component_builder.go      # Component factory (Phase 3)
└── component_test.go         # Unit tests (Phase 6)
```

## Files to Modify

```
src/internal/service/
└── routing_service.go        # Use components for apply (Phase 4)

src/internal/api/
└── check.go                  # Use components for self-check (Phase 5)

src/internal/commands/
├── apply.go                  # May need updates (Phase 4)
└── self_check.go             # Use components for CLI check (Phase 5)
```

## Current Status

**Phase 1**: ✅ Complete
**Phase 2**: ✅ Complete
**Phase 3**: ✅ Complete
**Phase 4**: ✅ Complete
**Phase 5**: ✅ Complete
**Phase 6**: ⏸️ Pending (testing)
**Phase 7**: ✅ Partial (old helpers removed, optional migrations pending)

**🎉 CORE REFACTORING COMPLETE! 🎉**

The NetworkingComponent abstraction is now fully integrated:
- ✅ Apply logic uses components
- ✅ Self-check logic uses components
- ✅ Both use IDENTICAL component building and checking
- ✅ Zero breaking changes to existing functionality

**Optional Next Steps**:
1. Add unit tests (Phase 6)
2. Migrate CLI `self_check.go` to use components
3. Remove unused PersistentConfigManager/RoutingConfigManager
4. Add more inline documentation

## Completed Work

### Commits Made:

1. **b39951c**: NetworkingComponent abstraction infrastructure (Phase 1-3)
   - Created all component types (IPSet, IPRule, IPRoute, IPTables)
   - Created ComponentBuilder with interface selector integration
   - Added comprehensive documentation

2. **3c64d66**: Self-check JSON API refactoring (Phase 5 partial)
   - Migrated checkIPSetSelfJSON() to use components
   - Added intelligent diagnostic messages (stale vs missing)
   - Fixed import cycle by using concrete keenetic.Client

3. **71f71e6**: Complete Phase 5 - SSE mode migration
   - Migrated checkIPSetSelfSSE() to use components
   - Removed checkIPTablesRule() and checkIPTablesRuleJSON()
   - Made SSE and JSON modes identical in logic (~150 lines removed)

4. **455150e**: Complete Phase 4 - Apply logic refactoring
   - Migrated ApplyPersistentConfig() to use ComponentBuilder
   - Migrated ApplyRoutingConfig() to use ComponentBuilder
   - Migrated UndoConfig() to use ComponentBuilder
   - Added automatic stale route cleanup

5. **[pending]**: Complete Phase 6 - Test suite
   - Added comprehensive unit tests for all component types
   - Added integration tests for ComponentBuilder
   - Fixed ComponentBuilder to skip IPTables when no rules configured
   - Added graceful test skipping when iptables binary not available
   - All tests pass (or skip gracefully) in environments without iptables

### Key Achievements:
- ✅ Eliminated ALL direct command execution in API layer
- ✅ Unified apply and check logic - SAME components, SAME checks
- ✅ Intelligent routing diagnostics (stale/missing/active/inactive)
- ✅ Automatic stale route cleanup in ApplyRoutingConfig
- ✅ Type-safe, testable architecture
- ✅ Zero breaking changes to existing functionality
- ✅ Code reduction: ~150 lines of duplicate logic removed

## Notes

- All components reuse existing types from networking package (IPSet, IPRule, Route, IPTablesRules)
- No breaking changes to existing API - this is purely internal refactoring
- The `GetCommand()` method provides backward compatibility for debugging
- Interface selector is injected into route components to determine ShouldExist()
- Import cycle resolved by accepting *keenetic.Client directly instead of domain interface
- Both SSE and JSON self-check modes now use components (identical logic)
- Apply logic and self-check logic use the same ComponentBuilder

---

*Last Updated: 2025-11-19*
*Status: **CORE REFACTORING COMPLETE** - Phases 1-6 complete, Phase 7 partial*
