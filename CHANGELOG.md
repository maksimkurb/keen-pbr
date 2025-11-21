# Changelog

All notable changes to this project will be documented in this file.

## [3.0.0] - Planned Release

### 🚀 New Features

- **Web Interface**: A modern, responsive web UI for managing lists, routing rules, and settings.
- **REST API**: A full-featured REST API (`/api/v1`) enabling programmatic control of all application functions.
  - **Lists Management**: CRUD operations for URL, file, and inline lists.
  - **IPSets Management**: Create and manage routing rules (ipsets) with complex criteria.
  - **System Status**: Real-time service status and version information.
- **Network Diagnostics**:
  - **Routing Check**: Verify how specific hosts are routed through the current configuration.
  - **Real-time Tools**: Ping and Traceroute tools with Server-Sent Events (SSE) streaming.
  - **Split-DNS Check**: New diagnostic tool to verify DNS routing configuration.
  - **Self Check**: Comprehensive system health check verifying config, network state, and router connectivity.
- **Server Command**: New `keen-pbr server` command to start the API server and host the web UI.

### 🏗 Architecture & Refactoring

- **Standard Go Layout**: Project structure reorganized into `src/internal/` following standard Go conventions.
- **Dependency Injection**:
  - Replaced global state with a proper Dependency Injection (DI) container (`AppDependencies`).
  - Improved testability and modularity across the codebase.
- **Service Layer**: Introduced a dedicated `service/` layer to encapsulate business logic, separating it from CLI commands and domain implementation.
- **Keenetic Client**:
  - Completely rewritten `keenetic` package.
  - Improved caching mechanism for RCI requests.
  - Better error handling and interface abstraction.
- **Configuration**:
  - Centralized configuration loading and validation.
  - Added `upgrade_config.go` for smooth configuration migration.

### 🛠 Improvements

- **Error Handling**: Standardized error reporting with structured error types.
- **Performance**: Optimized list processing and IP set operations.
- **Documentation**:
  - Comprehensive `API.md` documentation.
  - Updated `CONTEXT.md` reflecting the new architecture.

### ⚠️ Breaking Changes

- **Internal API**: The internal Go API has changed significantly due to the introduction of DI and the Service layer.
- **Configuration**: While backward compatibility is maintained, strict validation is now enforced. Invalid configuration fields that were previously ignored may now cause errors.

---

## [2.2.2] - Previous Release

- Maintenance release with minor fixes and dependency updates.
