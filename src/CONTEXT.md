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

- `outbound_table.go` - OutboundTable interface with implementations:
  - `StaticOutboundTable` - Single outbound reference
  - `URLTestOutboundTable` - Multiple outbounds with health checking
  - Custom JSON marshaling for polymorphic types

- `rule.go` - Routing rule entity with:
  - Fields: id, name, enabled, priority, lists, outboundTable
  - References List and OutboundTable by ID
  - Custom JSON marshaling

**Configuration Management** (`internal/config/`):

- `config.go` - Thread-safe configuration with RWMutex
  - Main Config struct with DNS, Outbounds, OutboundTables, Rules
  - CRUD operations for all entities
  - JSON persistence with polymorphic type handling

**Service Management** (`internal/service/`):

- `service.go` - Service lifecycle control
  - Operations: start, stop, restart, enable, disable
  - Status tracking

**API Server** (`internal/api/`):

- `api.go` - Main HTTP server with routing setup
  - CORS headers for `/v1/*` endpoints only
  - Static file serving for UI

- `service.go` - Service control endpoints
- `rules.go` - Rules CRUD endpoints
- `outbound_tables.go` - Outbound tables CRUD endpoints
- `config.go` - Configuration management endpoint
- `static.go` - Embedded UI serving using `embed.FS`

**Build Configuration:**

- `Makefile` - Build targets for various architectures
- `build.sh` - Automated build script with size reporting
- Size optimization: `-ldflags="-s -w"`, UPX compression

### Frontend (React)

Located in `src/keen-pbr-ui/`

**Configuration:**

- `rspack.config.ts` - Rspack bundler configuration
  - Dev server on port 3030
  - Proxy for `/v1` to `localhost:8080`
  - PostCSS loader with Tailwind CSS v4 and autoprefixer
  - TypeScript support with SWC

**Source Files** (`src/`):

- `main.tsx` - React entry point
- `App.tsx` - React Router setup with routes:
  - `/` - Dashboard
  - `/rules` - Rules management
  - `/outbound-tables` - Outbound tables management
  - `/config` - Configuration

- `types/index.ts` - TypeScript types matching Go models exactly
  - All entity types with proper discriminated unions

- `api/client.ts` - API client
  - Base URL from `VITE_API_BASE_URL` or empty string (same-origin)
  - Methods for all API endpoints

- `components/Layout.tsx` - Navigation layout with sidebar

- `pages/Dashboard.tsx` - Service control interface
- `pages/Rules.tsx` - Rules management table
- `pages/OutboundTables.tsx` - Outbound tables management
- `pages/Configuration.tsx` - Configuration editor

**Styling:**

- `src/index.css` - Tailwind CSS directives only
- Tailwind CSS v4 with `@tailwindcss/postcss` plugin
- PostCSS with autoprefixer

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

**Rules:**
- `GET /v1/rules` - List all rules
- `POST /v1/rules` - Create rule
- `GET /v1/rules/{id}` - Get rule
- `PUT /v1/rules/{id}` - Update rule
- `DELETE /v1/rules/{id}` - Delete rule

**Outbound Tables:**
- `GET /v1/outbound-tables` - List all outbound tables
- `POST /v1/outbound-tables` - Create outbound table
- `GET /v1/outbound-tables/{id}` - Get outbound table
- `PUT /v1/outbound-tables/{id}` - Update outbound table
- `DELETE /v1/outbound-tables/{id}` - Delete outbound table

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
