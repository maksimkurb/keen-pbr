# Automatic TypeScript Type Generation with guts

This project uses [guts](https://github.com/coder/guts) to automatically generate TypeScript interfaces from Go types, ensuring type safety between the backend and frontend.

## Overview

TypeScript types are automatically generated from Go packages using a custom generator program:

- **Source**: Go packages in `src/internal/` (api, config, service)
- **Output**: Single file `src/frontend/src/api/generated-types.ts`
- **Generator**: `cmd/generate-types/main.go`

This ensures:
1. ✅ Frontend types stay perfectly in sync with backend
2. ✅ Breaking API changes caught at compile time
3. ✅ Zero `any` types - everything is properly typed
4. ✅ No manual type duplication needed

## Why guts over tygo?

We switched from tygo to guts for better type generation:

| Feature | tygo | guts |
|---------|------|------|
| No `any` types | ❌ Some `any` | ✅ All properly typed |
| Single file output | ❌ Multiple files | ✅ One file |
| Cross-package types | ⚠️ Manual config | ✅ Automatic |
| Integration | External tool | ✅ Part of codebase |
| Type resolution | ⚠️ Can fail | ✅ Excellent |

## Quick Start

### Generate Types

```bash
# Using Make (recommended)
make generate-types

# Using Go directly
go run ./cmd/generate-types

# Using bun (from frontend directory)
cd src/frontend && bun run generate-types
```

### Build Frontend

```bash
# Automatically generates types first
make build-frontend
```

## How It Works

### 1. Generator Program

The generator is a Go program at [cmd/generate-types/main.go](../cmd/generate-types/main.go):

```go
// Include packages
golang.IncludeGenerate("github.com/maksimkurb/keen-pbr/src/internal/api")
golang.IncludeGenerate("github.com/maksimkurb/keen-pbr/src/internal/config")
golang.IncludeGenerate("github.com/maksimkurb/keen-pbr/src/internal/service")

// Map Go types to TypeScript
golang.IncludeCustom(map[string]string{
    "time.Time": "string",
})

// Exclude internal implementation types
golang.ExcludeCustom("github.com/maksimkurb/keen-pbr/src/internal/service.DNSService")
// ... more exclusions

// Generate
ts, _ := golang.ToTypescript()
ts.ApplyMutations(config.ExportTypes)
output, _ := ts.Serialize()
```

### 2. Type Mappings

Automatic type conversions:

| Go Type | TypeScript Type | Notes |
|---------|----------------|-------|
| `time.Time` | `string` | ISO 8601 format |
| `interface{}` | `unknown` | Type-safe unknown |
| `map[string]T` | `Record<string, T>` | Dictionary |
| `[]T` | `T[]` | Array |
| `*T` | `T \| null` | Nullable |
| Enums/Constants | `enum` | Proper enums |

### 3. Excluded Types

Internal types not needed in frontend:

- **Service implementations**: `DNSService`, `InterfaceService`
- **API handlers**: `Handler`, `ServiceManager`
- **Type aliases**: Duplicates avoided (use originals from service package)
- **Internal packages**: `keenetic` client types
- **Config internals**: `ConfigHasher`, `DNSProvider`

### 4. Generated Output

Single file with 580+ lines containing all types:

```typescript
// From api/types.go
export interface ListInfo {
    list_name: string;
    type: string;
    url?: string;
    stats: ListStatistics | null;
}

// From config/types.go
export interface IPSetConfig {
    ipset_name: string;
    lists: string[];
    ip_version: IPFamily;
    routing?: RoutingConfig | null;
}

// Enums preserved
export enum IPFamily {
    Ipv4 = 4,
    Ipv6 = 6
}
```

## When to Regenerate

Regenerate types whenever you:

✅ **MUST regenerate when:**
- Adding/removing fields in Go structs with JSON tags
- Changing field types in API response types
- Adding new API response/request types
- Renaming JSON field tags
- Changing Go type definitions used in API

❌ **NO need to regenerate for:**
- Go implementation changes (function bodies)
- Adding new unexported (private) types
- Changes to non-API Go code
- Documentation/comment updates only

## CI/CD Integration

### GitHub Actions

Both `build.yml` and `release.yml` automatically:

1. Check out code
2. Set up Go environment
3. Install dependencies (`go mod download`)
4. Generate types (`make generate-types`)
5. Build frontend with generated types

No external tool installation needed - guts is a Go dependency!

## Working with Generated Types

### Importing Types

```typescript
// Import from generated file
import type {
  ListInfo,
  IPSetConfig,
  GeneralConfig,
  StatusResponse,
} from "./generated-types";
```

### Client Usage

The [client.ts](../src/frontend/src/api/client.ts) imports and re-exports types:

```typescript
// Re-export for convenience
export type {
  ListInfo,
  IPSetConfig,
  GeneralConfig,
  // ... more types
};

// Use in API client
async getLists(): Promise<ListInfo[]> {
  const result = await this.request<ListsResponse>("GET", "/lists");
  return result.lists.filter((list): list is ListInfo => list !== null);
}
```

### Handling Nullable Fields

Generated types properly handle Go pointers:

```typescript
// Go: *GeneralConfig -> TS: GeneralConfig | null
async getSettings(): Promise<GeneralConfig> {
  const result = await this.request<SettingsResponse>("GET", "/settings");
  if (!result.general) {
    throw new Error("Settings not found");
  }
  return result.general; // Type narrowing
}
```

## Project Structure

```
keen-pbr-go/
├── cmd/
│   └── generate-types/
│       └── main.go              # Type generator program
├── src/
│   ├── internal/
│   │   ├── api/                 # API types (source)
│   │   ├── config/              # Config types (source)
│   │   └── service/             # Service types (source)
│   └── frontend/
│       └── src/api/
│           ├── generated-types.ts   # Generated (DO NOT EDIT)
│           └── client.ts            # Uses generated types
├── Makefile                     # `make generate-types`
└── go.mod                       # guts dependency
```

## Best Practices

### ✅ DO

- **Commit generated files**: They're part of your codebase
- **Regenerate after Go changes**: Run `make generate-types`
- **Use generated types**: Import from `generated-types.ts`
- **Add JSON tags**: `json:"field_name"` on Go structs
- **Export types**: Use capital letters for exported Go types

### ❌ DON'T

- **Edit generated files**: Changes will be overwritten
- **Duplicate types**: Import from generated file instead
- **Use `any`**: Generated types are fully typed
- **Skip generation**: CI will fail if types are stale

## Troubleshooting

### Types not updating?

```bash
# Clean and regenerate
rm src/frontend/src/api/generated-types.ts
make generate-types
```

### Frontend build fails?

1. Check if you regenerated types after Go changes
2. Look for breaking changes in Go type definitions
3. Update frontend code to match new types
4. This is intentional - catching breaking changes early!

### Parse errors during generation?

Parse errors in implementation code (not type definitions) are warnings and can be ignored:

```
ERROR parsing package error="..."
✓ Generated TypeScript types to ...
```

As long as you see the success message, types were generated correctly.

## Comparison: Before and After

### Before (Manual Types)

```typescript
// Manually maintained, can drift
export interface ListInfo {
  list_name: string;
  type: "url" | "file" | "hosts";
  // ... might be out of sync with Go
}
```

**Problems:**
- Manual synchronization required
- Can drift from backend types
- No compile-time safety
- `any` types for complex structures

### After (Generated Types)

```typescript
// Auto-generated, always in sync
export interface ListInfo {
  list_name: string;
  type: string;
  stats: ListStatistics | null;
  // ... matches Go exactly
}
```

**Benefits:**
- Automatically synchronized
- Type-safe at compile time
- Breaking changes caught early
- Fully typed, no `any`

## Advanced: Customizing Generation

To modify type generation, edit [cmd/generate-types/main.go](../cmd/generate-types/main.go):

### Add a Package

```go
packages := []string{
    "github.com/maksimkurb/keen-pbr/src/internal/api",
    "github.com/maksimkurb/keen-pbr/src/internal/newpkg", // Add here
}
```

### Add Type Mapping

```go
golang.IncludeCustom(map[string]string{
    "time.Time": "string",
    "uuid.UUID": "string", // Add custom mapping
})
```

### Exclude a Type

```go
excludeTypes := []string{
    "github.com/maksimkurb/keen-pbr/src/internal/api.InternalType",
}
```

## Dependencies

- **guts**: `github.com/coder/guts` - Type generator library
- **Go**: 1.23+ required for generator program

Added to `go.mod`:
```go
require (
    github.com/coder/guts v1.6.1
    github.com/coder/guts/config v1.6.1
)
```

## Related Files

- [cmd/generate-types/main.go](../cmd/generate-types/main.go) - Generator program
- [src/frontend/src/api/generated-types.ts](../src/frontend/src/api/generated-types.ts) - Generated types
- [src/frontend/src/api/client.ts](../src/frontend/src/api/client.ts) - API client using types
- [Makefile](../Makefile) - Build commands
- [go.mod](../go.mod) - Go dependencies
