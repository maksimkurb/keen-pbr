# Keenetic PBR Project Context

## Project Overview

This project implements a policy-based routing (PBR) system called "keen-pbr" with two main components:

1. **keen-pbr** - Go web server providing REST API and configuration management
2. **keen-pbr-ui** - React SPA for web interface

**Critical requirement**: Binary size optimization for router deployment

## Project Structure

### Backend (Go)

Located in `src/keen-pbr/`

**Entry Point:**
- `cmd/keen-pbr/main.go` - Application entry point with graceful shutdown

**Data Models** (`internal/models/`):

- `dns.go` - DNS configuration with types: UDP, TLS, HTTPS
  - Fields: type, server, port, path, throughOutbound

- `list.go` - List interface with implementations:
  - `LocalList` - Local file-based list
  - `RemoteList` - Remote URL-based list with update interval
  - Custom JSON marshaling for polymorphic types

- `outbound.go` - Outbound interface with implementations:
  - `InterfaceOutbound` - Physical network interface
  - `ProxyOutbound` - SOCKS5/HTTP proxy with authentication
  - Custom JSON marshaling for polymorphic types

- `rule.go` - Routing rule entity with inline OutboundTable:
  - Fields: id, name, enabled, priority, customDnsServers, lists, outboundTable
  - Includes OutboundTable interface with implementations:
    - `StaticOutboundTable` - Single outbound reference by tag
    - `URLTestOutboundTable` - Multiple outbounds with health checking and testUrl
  - Custom JSON marshaling for both Rule and OutboundTable types
  - Validation ensures all referenced outbound tags exist

**Configuration Management** (`internal/config/`):

- `config.go` - Thread-safe configuration with RWMutex
  - Main Config struct with Outbounds and Rules (no separate OutboundTables)
  - Individual GET operations for Rules and Outbounds
  - Bulk replace operations: `ReplaceAllRules()` and `ReplaceAllOutbounds()`
  - Rule replacement validates all referenced outbound tags exist
  - JSON persistence with polymorphic type handling

**Service Management** (`internal/service/`):

- `service.go` - Service lifecycle control
  - Operations: start, stop, restart, enable, disable
  - Status tracking

**API Server** (`internal/api/`):

- `api.go` - Main HTTP server with routing setup
  - CORS headers for `/v1/*` endpoints only
  - Static file serving for UI
  - Server struct includes configPath for saving on bulk updates

- `service.go` - Service control endpoints
- `bulk.go` - Bulk update endpoints (PUT with arrays)
  - `handleOutboundsBulk` - GET all / PUT replace all outbounds
  - `handleRulesBulk` - GET all / PUT replace all rules with validation
- `rules.go` - Individual rule GET endpoint
- `outbounds.go` - Individual outbound GET endpoint
- `config.go` - Configuration management endpoint
- `static.go` - Embedded UI serving using `embed.FS`

**Build Configuration:**

- `Makefile` - Build targets for various architectures
- `build.sh` - Automated build script with size reporting
- Size optimization: `-ldflags="-s -w"`, UPX compression

### Frontend (React)

Located in `src/keen-pbr-ui/`

**Configuration:**

- `rsbuild.config.ts` - Rsbuild bundler configuration
  - Dev server on port 3030
  - Proxy for `/v1` to `localhost:8080`
  - PostCSS tools configuration with Tailwind CSS v4 and autoprefixer
  - TypeScript support with SWC

**Source Files** (`src/`):

- `main.tsx` - React entry point
- `App.tsx` - React Router setup with routes:
  - `/` - Dashboard
  - `/rules` - Rules management with inline OutboundTable editor
  - `/outbounds` - Outbounds management
  - `/config` - Configuration

- `types/index.ts` - TypeScript types matching Go models exactly
  - All entity types with proper discriminated unions
  - NetworkInterface type for interface info

- `api/client.ts` - API client
  - Base URL from `VITE_API_BASE_URL` or empty string (same-origin)
  - `serviceAPI` - Service control endpoints
  - `rulesAPI` - Rules bulk update (`bulkUpdate` with array, `getAll`, `getOne`)
  - `outboundsAPI` - Outbounds bulk update (`bulkUpdate` with array, `getAll`, `getOne`)
  - `infoAPI` - System information (interfaces)
  - `configAPI` - Full configuration management

- `components/Layout.tsx` - Navigation layout with links to all pages

- `pages/Dashboard.tsx` - Service control interface
- `pages/Rules.tsx` - Rules management with card-based layout and inline OutboundTable editor
  - Supports both Static and URL Test outbound tables
  - Bulk save with validation
- `pages/Outbounds.tsx` - Outbounds management with card-based layout
  - Bulk save operation for all outbounds at once
- `pages/Configuration.tsx` - Configuration editor

- `components/OutboundCard.tsx` - Reusable outbound card component

**Styling:**

- `src/index.css` - Tailwind CSS directives only
- Tailwind CSS v4 with `@tailwindcss/postcss` plugin
- PostCSS with autoprefixer

**UI Design Principles:**

1. **Inline Editing Pattern (Cloudflare-style)**:
   - Tables have two states per row: view mode and edit mode
   - View mode: Clean display with formatted data and Edit/Delete buttons
   - Edit mode: Blue-highlighted row (`bg-blue-50`) with form inputs and Save/Cancel buttons
   - New entries appear at the top of the table in edit mode
   - No modal dialogs - all editing happens inline in the table

2. **Form Input Standards**:
   - Always use explicit `<label>` elements for inputs, not placeholders
   - Labels should be visible and positioned above or beside the input
   - Placeholders may be used for examples, but not as primary labels
   - Form structure: label → input relationship for accessibility

3. **Visual Feedback**:
   - Edit rows have blue background for clear differentiation
   - Hover effects on interactive elements
   - Type badges for categorization
   - Transition animations on buttons
   - Clear error messaging with red background

4. **Button Patterns**:
   - Primary actions: Blue (`bg-blue-600`)
   - Secondary actions: Gray (`bg-gray-200`)
   - Destructive actions: Red text (`text-red-600`)
   - Consistent spacing and sizing

## Technology Stack

### Backend
- Go (stdlib only, no frameworks for size optimization)
- `net/http` for HTTP server
- `embed.FS` for embedding UI
- JSON for configuration persistence

### Frontend
- React 19
- TypeScript
- rspack (fast bundler)
- Tailwind CSS v4 with PostCSS
- React Router
- bun (package manager)

## API Endpoints

All API endpoints use `/v1/` prefix:

**Service Control:**
- `GET /v1/service/status` - Get service status
- `POST /v1/service/start` - Start service
- `POST /v1/service/stop` - Stop service
- `POST /v1/service/restart` - Restart service
- `POST /v1/service/enable` - Enable autostart
- `POST /v1/service/disable` - Disable autostart

**Rules (Bulk + Individual GET):**
- `GET /v1/rules` - List all rules as Record<string, Rule>
- `PUT /v1/rules` - Bulk replace ALL rules (accepts array, validates outbound references, saves config)
- `GET /v1/rules/{id}` - Get individual rule

**Outbounds (Bulk + Individual GET):**
- `GET /v1/outbounds` - List all outbounds as Record<string, Outbound>
- `PUT /v1/outbounds` - Bulk replace ALL outbounds (accepts array, saves config)
- `GET /v1/outbounds/{tag}` - Get individual outbound

**Network Info:**
- `GET /v1/info/interfaces` - List network interfaces with IPs and status

**Configuration:**
- `GET /v1/config` - Get full configuration
- `PUT /v1/config` - Update full configuration

## Key Implementation Details

1. **Polymorphic JSON Handling**: All interfaces (List, Outbound, OutboundTable) use custom JSON marshaling with type discriminator field

2. **Thread Safety**: Configuration uses `sync.RWMutex` for concurrent access

3. **CORS**: Only applied to `/v1/*` API endpoints, not static files

4. **Embedded UI**: React build is embedded in Go binary using `//go:embed dist/*`

5. **Size Optimization**:
   - No external frameworks
   - Stdlib-only implementation
   - Build flags: `-ldflags="-s -w"`
   - Optional UPX compression

6. **Development Workflow**:
   - UI dev server proxies API calls to Go backend
   - Go backend serves embedded UI in production

## Build Process

1. Build UI: `cd src/keen-pbr-ui && bun run build`
2. Copy UI to Go: `cp -r dist ../keen-pbr/internal/api/`
3. Build Go binary: `cd src/keen-pbr && go build -ldflags="-s -w" ./cmd/keen-pbr`
4. Optional: Compress with UPX

Or use automated script: `src/keen-pbr/build.sh`
