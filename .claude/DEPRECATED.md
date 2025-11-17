# Deprecated Code Refactoring Plan

This document outlines all deprecated code in the keen-pbr codebase and provides a step-by-step plan to remove them.

## Overview

The codebase contains deprecated code from two main migration efforts:
1. **Configuration Migration**: Old TOML field names that were renamed for clarity
2. **Dependency Injection Migration**: Global state functions being replaced with instance methods

## Deprecated Configuration Fields

### Status: ✅ Can be removed safely
These fields are only used during config upgrade and have auto-migration logic.

### 1. `RoutingConfig.DeprecatedInterface` (string → []string)

**Location**: `src/internal/config/types.go:51`

```go
type RoutingConfig struct {
    Interfaces []string // New field
    DeprecatedInterface string `toml:"interface,omitempty"` // OLD
}
```

**Migration Logic**: `src/internal/config/config.go:104-111`
- Automatically converts `interface` → `interfaces` array
- Already cleared after migration

**Removal Plan**:
1. Remove field from `RoutingConfig` struct
2. Remove migration logic in `UpgradeConfig()`
3. Remove test `TestUpgradeConfig_DeprecatedInterface`

---

### 2. `IPSetConfig.DeprecatedLists` (nested → top-level)

**Location**: `src/internal/config/types.go:34`

```go
type IPSetConfig struct {
    Lists []string // New field (references list names)
    DeprecatedLists []*ListSource `toml:"list,omitempty"` // OLD (nested lists)
}
```

**Migration Logic**: `src/internal/config/config.go:114-131`
- Converts nested `ipset.list` → top-level `list` section
- Generates new list names: `{ipset_name}-{list_name}`
- Already cleared after migration

**Removal Plan**:
1. Remove field from `IPSetConfig` struct
2. Remove migration logic in `UpgradeConfig()`
3. Remove test `TestUpgradeConfig_DeprecatedLists`

---

### 3. `ListSource.DeprecatedName` (name → list_name)

**Location**: `src/internal/config/types.go:60`

```go
type ListSource struct {
    ListName string // New field
    DeprecatedName string `toml:"name,omitempty"` // OLD
}
```

**Migration Logic**: `src/internal/config/config.go:142-149`
- Automatically converts `name` → `list_name`
- Already cleared after migration

**Removal Plan**:
1. Remove field from `ListSource` struct
2. Remove migration logic in `UpgradeConfig()`
3. Remove fallback in `ListSource.Name()` method (line 81-87)
4. Remove test cases checking deprecated field

---

## Deprecated Keenetic Client Functions

### Status: ⚠️ Still in use - needs careful migration

### 4. Global State Functions (keenetic/rci.go)

**Problem**: These functions use global `defaultClient` instead of accepting a client instance.

#### Functions to Remove:

##### a) `fetchAndDeserialize[T any](endpoint string)`
**Location**: `src/internal/keenetic/rci.go:46-48`
- Uses global `defaultClient`
- **Replacement**: `fetchAndDeserializeForClient[T](client, endpoint)`

##### b) `fetchAndDeserializeWithRetry[T any](endpoint string)`
**Location**: `src/internal/keenetic/rci.go:51-66`
- Uses global `fetchAndDeserialize`
- **Replacement**: Implement retry logic in `Client` methods

##### c) `RciShowInterfaceMappedById()`
**Location**: `src/internal/keenetic/rci.go:71-73`
- **Replacement**: `client.GetInterfaces()`
- **Usage**: Only in tests

##### d) `RciShowInterfaceMappedByIPNet()`
**Location**: `src/internal/keenetic/rci.go:78-80`
- Wrapper around `defaultClient.GetInterfaces()`
- **Replacement**: Use `client.GetInterfaces()` directly
- **Usage**: `src/internal/commands/self_check.go:110`

##### e) `ParseDnsProxyConfig(config string)`
**Location**: `src/internal/keenetic/rci.go:85-87`
- Wrapper around `parseDNSProxyConfig`
- **Replacement**: Use `parseDNSProxyConfig` directly or make it public
- **Usage**: Only in tests (`rci_test.go`)

##### f) `RciShowDnsServers()`
**Location**: `src/internal/keenetic/rci.go:92-112`
- Uses global `fetchAndDeserializeWithRetry`
- **Replacement**: `client.GetDNSServers()`
- **Usage**: None found

**Removal Plan**:
1. Update `self_check.go` to use `client.GetInterfaces()` instead of `RciShowInterfaceMappedByIPNet()`
2. Update tests to use `parseDNSProxyConfig` directly
3. Remove all global functions
4. Consider making `parseDNSProxyConfig` public if needed externally

---

### 5. Default Client Functions (keenetic/client.go)

**Problem**: Package-level functions that use global `defaultClient`.

##### a) `GetDefaultClient()`
**Location**: `src/internal/keenetic/client.go:188-190`
- Returns global `defaultClient`
- **Replacement**: Pass `*Client` explicitly
- **Usage**: `src/internal/networking/network.go:16` (for legacy functions)

##### b) `SetDefaultClient(client HTTPClient)`
**Location**: `src/internal/keenetic/client.go:169-171`
- Sets HTTP client on global instance
- **Replacement**: Create client with `NewClient(httpClient)`
- **Usage**: Only in tests

##### c) `GetVersion()` (global)
**Location**: `src/internal/keenetic/client.go:194-196`
- **Replacement**: `client.GetVersion()`
- **Usage**: None found

##### d) `GetInterfaces()` (global)
**Location**: `src/internal/keenetic/client.go:201-203`
- **Replacement**: `client.GetInterfaces()`
- **Usage**: None found

##### e) `GetDNSServers()` (global)
**Location**: `src/internal/keenetic/client.go:208-210`
- **Replacement**: `client.GetDNSServers()`
- **Usage**: None found

**Removal Plan**:
1. Remove all global functions from `client.go`
2. Update tests to create client instances
3. Remove `defaultClient` variable

---

### 6. Internal Deprecated Method (keenetic/interfaces.go)

##### `getSystemNameForInterface(interfaceID string)`
**Location**: `src/internal/keenetic/interfaces.go:74-81`
- Single interface query (N requests for N interfaces)
- **Replacement**: `getSystemNamesForInterfacesBulk(interfaceIDs []string)` (1 request)
- **Usage**: None - already migrated to bulk method

**Removal Plan**:
1. Remove method (no usages found)

---

## Deprecated Networking Functions

### Status: ⚠️ Used by tests and self-check command

### 7. Legacy Networking Functions (networking/network.go)

All these functions use global `defaultManager` instead of dependency injection.

##### a) `ApplyNetworkConfiguration(config, onlyRoutingForInterface)`
**Location**: `src/internal/networking/network.go:24-53`
- **Replacement**: Use `Manager.ApplyPersistentConfig()` + `Manager.ApplyRoutingConfig()`
- **Usage**: `network_test.go:69`

##### b) `applyIpsetNetworkConfiguration(ipset)`
**Location**: `src/internal/networking/network.go:58-70`
- Internal helper for above
- **Usage**: Only by `ApplyNetworkConfiguration`

##### c) `ApplyPersistentNetworkConfiguration(config)`
**Location**: `src/internal/networking/network.go:77-79`
- **Replacement**: `Manager.ApplyPersistentConfig(config.IPSets)`
- **Usage**: None found

##### d) `ApplyRoutingConfiguration(config)`
**Location**: `src/internal/networking/network.go:86-88`
- **Replacement**: `Manager.ApplyRoutingConfig(config.IPSets)`
- **Usage**: None found

##### e) `ChooseBestInterface(ipset, keeneticIfaces)`
**Location**: `src/internal/networking/network.go:96-129`
- **Replacement**: `InterfaceSelector.ChooseBest(ipset)`
- **Usage**:
  - `network_test.go:126`
  - `src/internal/commands/self_check.go:117`

**Removal Plan**:
1. Update `self_check.go` to create an `InterfaceSelector` instance
2. Update tests to use `Manager` directly
3. Remove all deprecated functions
4. Remove global `defaultManager` variable (line 16)

---

## Migration Timeline

### Phase 1: Configuration Fields (Safe - No External Impact)
**Estimated effort**: 2 hours

1. Remove deprecated config fields from structs
2. Remove migration logic and tests
3. Update documentation

**Breaking Change**: Users must run config upgrade before updating to this version.

**Recommendation**: Add a startup check that fails if deprecated fields are detected, with instructions to run `keen-pbr upgrade-config` first.

---

### Phase 2: Self-Check Command (Single Usage Point)
**Estimated effort**: 1 hour

1. Refactor `self_check.go` to use dependency injection:
   ```go
   // OLD
   keeneticIfaces, err := keenetic.RciShowInterfaceMappedByIPNet()
   chosenIface, err := networking.ChooseBestInterface(ipsetCfg, keeneticIfaces)

   // NEW
   client := keenetic.NewClient(nil)
   selector := networking.NewInterfaceSelector(client)
   chosenIface, err := selector.ChooseBest(ipsetCfg)
   ```

---

### Phase 3: Keenetic Client (Medium Impact)
**Estimated effort**: 3 hours

1. Remove global functions from `keenetic/rci.go`
2. Remove global functions from `keenetic/client.go`
3. Remove `defaultClient` variable
4. Update all tests to use `NewClient()`
5. Make `parseDNSProxyConfig` public if needed by tests

**Breaking Change**: External code using global functions must migrate.

---

### Phase 4: Networking Functions (Low Impact)
**Estimated effort**: 2 hours

1. Update tests in `network_test.go` to use `Manager` directly
2. Remove deprecated functions from `network.go`
3. Remove global `defaultManager` variable

**Breaking Change**: External code using legacy functions must migrate.

---

### Phase 5: Cleanup (Final Polish)
**Estimated effort**: 1 hour

1. Remove `getSystemNameForInterface` method (unused)
2. Search for any remaining "Deprecated" comments
3. Update `.claude/CONTEXT.md` to remove deprecated references
4. Run full test suite

---

## Total Estimated Effort: 9 hours

## Rollout Strategy

### Option A: Gradual Migration (Recommended)
- Release Phase 1 alone (config fields only)
- Wait one release cycle (users upgrade configs)
- Release Phase 2-5 together

**Pros**: Lower risk, users have time to migrate configs
**Cons**: Takes longer

### Option B: All at Once
- Release all phases together
- Provide migration script for config fields
- Update documentation with breaking changes

**Pros**: Faster cleanup, one breaking change
**Cons**: Higher risk, requires good documentation

---

## Breaking Changes Summary

### For Users
- Must run `keen-pbr upgrade-config` before upgrading
- Old config field names (`interface`, `name`, nested `list`) will no longer be auto-migrated

### For Developers
- Must use dependency injection instead of global functions
- Must pass client instances explicitly
- Tests must create manager/selector/client instances

---

## Testing Checklist

- [ ] All existing tests pass
- [ ] Config upgrade with deprecated fields fails gracefully
- [ ] Self-check command works with new DI pattern
- [ ] All commands work without global functions
- [ ] No "Deprecated" comments remain in code
- [ ] Documentation updated

---

## Notes

- Keep `UpgradeConfigCommand` for at least one more release after removing deprecated fields
- Add migration documentation in CHANGELOG.md
- Consider adding a deprecation notice in the release before removing
