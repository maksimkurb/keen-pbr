# Backend Refactoring: Step-by-Step Implementation Plan

This document provides the detailed implementation steps for rewriting the keen-pbr backend following the "Unified Controller" architecture from `.claude/PLAN.md`.

**Golden Rules:**
1. ❌ **NEVER modify files in `src/internal/`**
2. ✅ **Only write new code in `src/pkg/` or `src/cmd/`**
3. ✅ **Reference `src/internal/` files to understand current behavior**
4. ✅ **Preserve all existing functionality**

---

## Phase 1: Foundation

### Step 1.1: Create API Contract (`src/pkg/api/`)

**Purpose:** Define the unified request/response structs used by ALL interfaces (CLI, HTTP, IPC).

**Files to create:**

#### `src/pkg/api/types.go`
```go
package api

// Common types used across all requests/responses
type (
    // Status responses
    StatusResponse struct {
        Version      string            `json:"version"`
        ConfigHash   string            `json:"config_hash"`
        IsRunning    bool              `json:"is_running"`
        MemUsageMB   float64           `json:"mem_usage_mb"`
        UptimeSec    int64             `json:"uptime_sec"`
        Services     ServiceStatus     `json:"services"`
        IPSetsStatus []IPSetStatus     `json:"ipsets"`
    }

    ServiceStatus struct {
        NetworkingApplied bool `json:"networking_applied"`
        DNSRunning        bool `json:"dns_running"`
    }

    IPSetStatus struct {
        Name     string `json:"name"`
        Entries  int    `json:"entries"`
        IPFamily int    `json:"ip_family"`
    }
)
```

**Reference files:**
- `src/internal/api/types.go` - Copy ALL type definitions
- Ensure snake_case JSON tags match existing API

**Action items:**
1. Copy all request/response types from `src/internal/api/types.go`
2. Add any missing types found in handler files
3. Ensure all types are serializable (exported fields, JSON tags)
4. Create separate files for logical grouping:
   - `types_lists.go` - List-related types
   - `types_ipsets.go` - IPSet-related types
   - `types_settings.go` - Settings types
   - `types_checks.go` - Check/diagnostic types
   - `types_common.go` - Shared types

### Step 1.2: Create Memory Pool (`src/pkg/pool/`)

**Purpose:** Centralized buffer pooling to reduce GC pressure and memory usage.

**Files to create:**

#### `src/pkg/pool/buffer.go`
```go
package pool

import (
    "bytes"
    "sync"
)

// ByteBuffer pool for JSON encoding and DNS responses
var bufferPool = sync.Pool{
    New: func() interface{} {
        // 4KB pre-allocation (common MTU/page size)
        return bytes.NewBuffer(make([]byte, 0, 4096))
    },
}

// GetBuffer returns a zeroed buffer from the pool
func GetBuffer() *bytes.Buffer {
    buf := bufferPool.Get().(*bytes.Buffer)
    buf.Reset()
    return buf
}

// PutBuffer returns a buffer to the pool
// Buffers larger than 64KB are discarded to prevent memory leaks
func PutBuffer(buf *bytes.Buffer) {
    if buf.Cap() > 65536 {
        return
    }
    bufferPool.Put(buf)
}
```

#### `src/pkg/pool/dns.go`
```go
package pool

import (
    "sync"
)

// DNSMessagePool for DNS query/response buffers
// Reuse pattern from src/internal/dnsproxy/
var dnsMessagePool = sync.Pool{
    New: func() interface{} {
        return make([]byte, 512) // Standard DNS UDP size
    },
}

// GetDNSBuffer returns a DNS message buffer
func GetDNSBuffer() []byte {
    return dnsMessagePool.Get().([]byte)
}

// PutDNSBuffer returns a buffer to the pool
func PutDNSBuffer(buf []byte) {
    dnsMessagePool.Put(buf)
}
```

**Reference files:**
- `src/internal/dnsproxy/caching/records_cache.go` - Existing buffer pool usage

### Step 1.3: Create Error Types (`src/pkg/api/errors.go`)

```go
package api

// AppError represents a structured error response
type AppError struct {
    Code    string                 `json:"code"`
    Message string                 `json:"message"`
    Details map[string]interface{} `json:"details,omitempty"`
}

func (e *AppError) Error() string {
    return e.Message
}

// Common error constructors
func NewValidationError(msg string) *AppError {
    return &AppError{Code: "VALIDATION_ERROR", Message: msg}
}

func NewNotFoundError(resource string) *AppError {
    return &AppError{Code: "NOT_FOUND", Message: resource + " not found"}
}

func NewInternalError(msg string) *AppError {
    return &AppError{Code: "INTERNAL_ERROR", Message: msg}
}
```

**Reference files:**
- `src/internal/api/errors.go` - Existing error handling

---

## Phase 2: Controller Layer

### Step 2.1: Create Controller Interface (`src/pkg/controller/interface.go`)

**Purpose:** Define the contract that ALL transport layers (CLI, HTTP, IPC) will use.

```go
package controller

import (
    "context"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
)

// AppController defines ALL business operations available to any interface
// This is the SINGLE source of truth for application logic
type AppController interface {
    // Status & Info
    GetStatus(ctx context.Context) (*api.StatusResponse, error)
    GetInterfaces(ctx context.Context) ([]api.Interface, error)
    GetDNSServers(ctx context.Context) ([]api.DNSServer, error)

    // Lists Management
    ListLists(ctx context.Context) ([]api.ListInfo, error)
    GetList(ctx context.Context, name string) (*api.ListDetail, error)
    CreateList(ctx context.Context, req *api.CreateListRequest) error
    UpdateList(ctx context.Context, name string, req *api.UpdateListRequest) error
    DeleteList(ctx context.Context, name string) error
    DownloadLists(ctx context.Context, names []string) (*api.DownloadResult, error)

    // IPSets Management
    ListIPSets(ctx context.Context) ([]api.IPSetInfo, error)
    GetIPSet(ctx context.Context, name string) (*api.IPSetDetail, error)
    CreateIPSet(ctx context.Context, req *api.CreateIPSetRequest) error
    UpdateIPSet(ctx context.Context, name string, req *api.UpdateIPSetRequest) error
    DeleteIPSet(ctx context.Context, name string) error

    // Settings
    GetSettings(ctx context.Context) (*api.Settings, error)
    UpdateSettings(ctx context.Context, req *api.UpdateSettingsRequest) error

    // Service Control
    ControlService(ctx context.Context, action string) error

    // Network Checks
    CheckRouting(ctx context.Context, req *api.CheckRoutingRequest) (*api.RoutingCheckResult, error)
    StreamPing(ctx context.Context, req *api.PingRequest) (<-chan api.PingEvent, error)
    StreamTraceroute(ctx context.Context, req *api.TracerouteRequest) (<-chan api.TracerouteEvent, error)
    StreamSelfCheck(ctx context.Context) (<-chan api.SelfCheckEvent, error)
    StreamDNSCheck(ctx context.Context, req *api.DNSCheckRequest) (<-chan api.DNSCheckEvent, error)
}
```

**Reference files:**
- `src/internal/api/router.go` - All endpoint routes (extract operations)
- `src/internal/api/*.go` - All handler signatures

### Step 2.2: Create Controller Implementation (`src/pkg/controller/service.go`)

**Purpose:** Implement the business logic once, reused by all interfaces.

```go
package controller

import (
    "context"
    "github.com/maksimkurb/keen-pbr/src/internal/core"
    "github.com/maksimkurb/keen-pbr/src/internal/config"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
)

// Service implements AppController
// It REUSES existing engine layer (src/internal/*)
type Service struct {
    container    *core.AppDependencies
    configLoader *config.Loader
    // ... other dependencies from internal/
}

// NewService creates a controller instance
// Dependencies come from existing src/internal/ packages
func NewService(deps *core.AppDependencies) *Service {
    return &Service{
        container: deps,
        // Initialize with existing managers
    }
}

// GetStatus implements the logic ONCE
// Used by: HTTP GET /api/v1/status, CLI 'keen-pbr status', IPC 'status' command
func (s *Service) GetStatus(ctx context.Context) (*api.StatusResponse, error) {
    // Extract logic from src/internal/api/status.go GetStatus handler
    // Return pointer to avoid struct copying (memory optimization)
    return &api.StatusResponse{
        Version:    "3.0.0",
        IsRunning:  true,
        // ... populate from existing managers
    }, nil
}
```

**Implementation strategy for each method:**

1. **Reference the corresponding handler** in `src/internal/api/*.go`
2. **Extract pure business logic** (remove HTTP-specific code)
3. **Call existing managers** from `src/internal/` (networking, keenetic, lists, etc.)
4. **Return API contract types** from `src/pkg/api/`
5. **Handle errors** consistently using `api.AppError`

**Files to create:**

- `src/pkg/controller/service.go` - Main service struct
- `src/pkg/controller/status.go` - Status & info methods
- `src/pkg/controller/lists.go` - List management methods
- `src/pkg/controller/ipsets.go` - IPSet management methods
- `src/pkg/controller/settings.go` - Settings methods
- `src/pkg/controller/service_control.go` - Service control methods
- `src/pkg/controller/checks.go` - Network check methods (including SSE streams)

**Reference mapping:**

| Controller Method | Extract Logic From | Calls (src/internal/) |
|------------------|-------------------|---------------------|
| GetStatus | api/status.go:GetStatus | networking.Manager, dnsproxy |
| GetInterfaces | api/interfaces.go:GetInterfaces | service.InterfaceService |
| GetDNSServers | api/dns.go:GetDNSServers | service.DNSService |
| ListLists | api/lists.go:GetLists | config.Config |
| GetList | api/lists.go:GetList | config.Config, lists.Manager |
| CreateList | api/lists.go:CreateList | config.Config, config.Validator |
| UpdateList | api/lists.go:UpdateList | config.Config, config.Validator |
| DeleteList | api/lists.go:DeleteList | config.Config |
| DownloadLists | api/lists.go:DownloadLists | lists.Downloader |
| ListIPSets | api/ipsets.go:GetIPSets | config.Config |
| GetIPSet | api/ipsets.go:GetIPSet | config.Config, networking.Manager |
| CreateIPSet | api/ipsets.go:CreateIPSet | config.Config, config.Validator |
| UpdateIPSet | api/ipsets.go:UpdateIPSet | config.Config, config.Validator |
| DeleteIPSet | api/ipsets.go:DeleteIPSet | config.Config, networking.Manager |
| GetSettings | api/settings.go:GetSettings | config.Config |
| UpdateSettings | api/settings.go:UpdateSettings | config.Config, config.Validator |
| ControlService | api/service.go:ControlService | commands.ServiceManager |
| CheckRouting | api/check.go:CheckRouting | networking.Manager |
| StreamPing | api/check.go:Ping | networking.Manager (exec ping) |
| StreamTraceroute | api/check.go:Traceroute | networking.Manager (exec traceroute) |
| StreamSelfCheck | api/check.go:SelfCheck | config.Validator, networking.Manager |
| StreamDNSCheck | api/dns.go:StreamDNSCheck | dnsproxy.Proxy |

### Step 2.3: Extract Critical Business Logic Examples

**Example 1: List Management (from api/lists.go)**

Current code (518 lines in handler):
```go
// src/internal/api/lists.go (READ-ONLY reference)
func (h *Handler) CreateList(w http.ResponseWriter, r *http.Request) {
    // 1. Decode JSON
    // 2. Load config
    // 3. Validate list
    // 4. Check duplicates
    // 5. Add to config
    // 6. Save config
    // 7. Trigger service restart
    // 8. Write JSON response
}
```

New controller method:
```go
// src/pkg/controller/lists.go (NEW file)
func (s *Service) CreateList(ctx context.Context, req *api.CreateListRequest) error {
    // Load config using existing loader
    cfg, err := s.configLoader.Load()
    if err != nil {
        return api.NewInternalError(err.Error())
    }

    // Validate using existing validator
    if err := validateListRequest(req); err != nil {
        return api.NewValidationError(err.Error())
    }

    // Check duplicates
    for _, list := range cfg.Lists {
        if list.Name == req.Name {
            return api.NewValidationError("list already exists")
        }
    }

    // Add to config (reuse existing config types from src/internal/config)
    cfg.Lists = append(cfg.Lists, convertToInternalList(req))

    // Save config
    if err := s.configLoader.Save(cfg); err != nil {
        return api.NewInternalError(err.Error())
    }

    // Trigger restart (if daemon running)
    if s.serviceManager != nil {
        s.serviceManager.Restart()
    }

    return nil
}
```

**Example 2: SSE Streaming (from api/check.go)**

Current code (uses chi SSE):
```go
// src/internal/api/check.go (READ-ONLY reference)
func (h *Handler) Ping(w http.ResponseWriter, r *http.Request) {
    // 1. Parse query params
    // 2. Set SSE headers
    // 3. Create flusher
    // 4. Exec ping command
    // 5. Stream output to client
}
```

New controller method:
```go
// src/pkg/controller/checks.go (NEW file)
func (s *Service) StreamPing(ctx context.Context, req *api.PingRequest) (<-chan api.PingEvent, error) {
    // Return a channel - transport layer handles SSE encoding
    eventChan := make(chan api.PingEvent, 10)

    go func() {
        defer close(eventChan)

        // Execute ping (reuse logic from api/check.go)
        cmd := exec.CommandContext(ctx, "ping", "-c", "10", req.Host)
        stdout, _ := cmd.StdoutPipe()
        cmd.Start()

        scanner := bufio.NewScanner(stdout)
        for scanner.Scan() {
            select {
            case <-ctx.Done():
                return
            case eventChan <- api.PingEvent{Line: scanner.Text()}:
            }
        }
        cmd.Wait()
    }()

    return eventChan, nil
}
```

**Key extraction patterns:**

1. **Config operations:** Always use `s.configLoader.Load()` and `s.configLoader.Save()`
2. **Validation:** Extract validation logic into separate functions
3. **Streaming:** Return channels, let transport layer encode as SSE
4. **Error handling:** Use `api.AppError` with proper error codes
5. **Context cancellation:** Respect `ctx` for cancellable operations

---

## Phase 3: HTTP Transport Layer

### Step 3.1: Create HTTP Server (`src/pkg/transport/http/server.go`)

**Purpose:** Stdlib HTTP server that wraps the controller.

```go
package http

import (
    "net/http"
    "github.com/maksimkurb/keen-pbr/src/pkg/controller"
    "github.com/maksimkurb/keen-pbr/src/pkg/pool"
)

// Server wraps the AppController for HTTP transport
type Server struct {
    ctrl controller.AppController
    mux  *http.ServeMux
}

// NewServer creates an HTTP server using Go 1.22+ pattern matching
func NewServer(ctrl controller.AppController) *Server {
    s := &Server{
        ctrl: ctrl,
        mux:  http.NewServeMux(),
    }
    s.registerRoutes()
    return s
}

// ServeHTTP implements http.Handler
func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
    s.mux.ServeHTTP(w, r)
}

// registerRoutes maps HTTP routes to controller methods
func (s *Server) registerRoutes() {
    // Use Go 1.22+ pattern matching (no Chi!)
    s.mux.HandleFunc("GET /api/v1/status", s.handleGetStatus)
    s.mux.HandleFunc("GET /api/v1/interfaces", s.handleGetInterfaces)
    s.mux.HandleFunc("GET /api/v1/dns-servers", s.handleGetDNSServers)

    // Lists
    s.mux.HandleFunc("GET /api/v1/lists", s.handleListLists)
    s.mux.HandleFunc("POST /api/v1/lists", s.handleCreateList)
    s.mux.HandleFunc("GET /api/v1/lists/{name}", s.handleGetList)
    s.mux.HandleFunc("PUT /api/v1/lists/{name}", s.handleUpdateList)
    s.mux.HandleFunc("DELETE /api/v1/lists/{name}", s.handleDeleteList)
    s.mux.HandleFunc("POST /api/v1/lists-download", s.handleDownloadLists)
    s.mux.HandleFunc("POST /api/v1/lists-download/{name}", s.handleDownloadList)

    // IPSets
    s.mux.HandleFunc("GET /api/v1/ipsets", s.handleListIPSets)
    s.mux.HandleFunc("POST /api/v1/ipsets", s.handleCreateIPSet)
    s.mux.HandleFunc("GET /api/v1/ipsets/{name}", s.handleGetIPSet)
    s.mux.HandleFunc("PUT /api/v1/ipsets/{name}", s.handleUpdateIPSet)
    s.mux.HandleFunc("DELETE /api/v1/ipsets/{name}", s.handleDeleteIPSet)

    // Settings
    s.mux.HandleFunc("GET /api/v1/settings", s.handleGetSettings)
    s.mux.HandleFunc("PATCH /api/v1/settings", s.handleUpdateSettings)

    // Service Control
    s.mux.HandleFunc("POST /api/v1/service", s.handleServiceControl)

    // Network Checks
    s.mux.HandleFunc("POST /api/v1/check/routing", s.handleCheckRouting)
    s.mux.HandleFunc("GET /api/v1/check/ping", s.handleStreamPing)
    s.mux.HandleFunc("GET /api/v1/check/traceroute", s.handleStreamTraceroute)
    s.mux.HandleFunc("GET /api/v1/check/self", s.handleStreamSelfCheck)
    s.mux.HandleFunc("GET /api/v1/check/split-dns", s.handleStreamDNSCheck)

    // Static files (frontend)
    // TODO: Serve embedded frontend
}
```

### Step 3.2: Create Generic Handler Wrappers (`src/pkg/transport/http/handlers.go`)

**Purpose:** Generic wrappers to reduce boilerplate, using Go generics.

```go
package http

import (
    "context"
    "encoding/json"
    "net/http"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
    "github.com/maksimkurb/keen-pbr/src/pkg/pool"
)

// handleJSON wraps a controller method that returns data
// Uses buffer pool to reduce GC pressure
func handleJSON[T any](fn func(context.Context) (T, error)) http.HandlerFunc {
    return func(w http.ResponseWriter, r *http.Request) {
        resp, err := fn(r.Context())
        if err != nil {
            writeError(w, err)
            return
        }

        // Use buffer pool for JSON encoding (memory optimization)
        buf := pool.GetBuffer()
        defer pool.PutBuffer(buf)

        if err := json.NewEncoder(buf).Encode(map[string]interface{}{"data": resp}); err != nil {
            writeError(w, api.NewInternalError("encoding failed"))
            return
        }

        w.Header().Set("Content-Type", "application/json")
        w.WriteHeader(http.StatusOK)
        buf.WriteTo(w) // Zero-copy write
    }
}

// handleJSONWithRequest wraps a controller method that takes a request body
func handleJSONWithRequest[TReq any](fn func(context.Context, *TReq) error) http.HandlerFunc {
    return func(w http.ResponseWriter, r *http.Request) {
        var req TReq
        if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
            writeError(w, api.NewValidationError("invalid JSON"))
            return
        }

        if err := fn(r.Context(), &req); err != nil {
            writeError(w, err)
            return
        }

        w.Header().Set("Content-Type", "application/json")
        w.WriteHeader(http.StatusOK)
        json.NewEncoder(w).Encode(map[string]interface{}{"data": "success"})
    }
}

// writeError writes an error response
func writeError(w http.ResponseWriter, err error) {
    appErr, ok := err.(*api.AppError)
    if !ok {
        appErr = api.NewInternalError(err.Error())
    }

    statusCode := http.StatusInternalServerError
    switch appErr.Code {
    case "VALIDATION_ERROR":
        statusCode = http.StatusBadRequest
    case "NOT_FOUND":
        statusCode = http.StatusNotFound
    }

    w.Header().Set("Content-Type", "application/json")
    w.WriteHeader(statusCode)
    json.NewEncoder(w).Encode(map[string]interface{}{"error": appErr})
}
```

### Step 3.3: Implement Handler Methods (`src/pkg/transport/http/handlers_*.go`)

**Purpose:** Thin wrappers that call controller methods.

**Pattern for simple GET:**
```go
// src/pkg/transport/http/handlers_status.go
func (s *Server) handleGetStatus(w http.ResponseWriter, r *http.Request) {
    handleJSON(s.ctrl.GetStatus)(w, r)
}
```

**Pattern for POST with body:**
```go
// src/pkg/transport/http/handlers_lists.go
func (s *Server) handleCreateList(w http.ResponseWriter, r *http.Request) {
    handleJSONWithRequest(func(ctx context.Context, req *api.CreateListRequest) error {
        return s.ctrl.CreateList(ctx, req)
    })(w, r)
}
```

**Pattern for path parameters:**
```go
func (s *Server) handleGetList(w http.ResponseWriter, r *http.Request) {
    name := r.PathValue("name") // Go 1.22+ pattern matching

    handleJSON(func(ctx context.Context) (*api.ListDetail, error) {
        return s.ctrl.GetList(ctx, name)
    })(w, r)
}
```

**Pattern for SSE streaming:**
```go
// src/pkg/transport/http/handlers_checks.go
func (s *Server) handleStreamPing(w http.ResponseWriter, r *http.Request) {
    host := r.URL.Query().Get("host")
    if host == "" {
        writeError(w, api.NewValidationError("host required"))
        return
    }

    // Get event channel from controller
    eventChan, err := s.ctrl.StreamPing(r.Context(), &api.PingRequest{Host: host})
    if err != nil {
        writeError(w, err)
        return
    }

    // Set SSE headers
    w.Header().Set("Content-Type", "text/event-stream")
    w.Header().Set("Cache-Control", "no-cache")
    w.Header().Set("Connection", "keep-alive")

    flusher, ok := w.(http.Flusher)
    if !ok {
        writeError(w, api.NewInternalError("streaming not supported"))
        return
    }

    // Stream events
    for event := range eventChan {
        buf := pool.GetBuffer()
        json.NewEncoder(buf).Encode(event)

        fmt.Fprintf(w, "data: %s\n\n", buf.String())
        pool.PutBuffer(buf)
        flusher.Flush()
    }
}
```

**Files to create:**

- `src/pkg/transport/http/handlers_status.go` - Status & info handlers
- `src/pkg/transport/http/handlers_lists.go` - List management handlers
- `src/pkg/transport/http/handlers_ipsets.go` - IPSet handlers
- `src/pkg/transport/http/handlers_settings.go` - Settings handlers
- `src/pkg/transport/http/handlers_service.go` - Service control handlers
- `src/pkg/transport/http/handlers_checks.go` - Check/diagnostic handlers (with SSE)

### Step 3.4: Implement Middleware (`src/pkg/transport/http/middleware.go`)

**Purpose:** Replace Chi middleware with stdlib equivalents.

```go
package http

import (
    "net"
    "net/http"
    "strings"
    "time"
)

// Middleware chain
func applyMiddleware(handler http.Handler) http.Handler {
    handler = recoveryMiddleware(handler)
    handler = loggingMiddleware(handler)
    handler = corsMiddleware(handler)
    handler = privateSubnetMiddleware(handler)
    return handler
}

// recoveryMiddleware catches panics
func recoveryMiddleware(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        defer func() {
            if err := recover(); err != nil {
                // Log panic
                http.Error(w, "Internal Server Error", 500)
            }
        }()
        next.ServeHTTP(w, r)
    })
}

// loggingMiddleware logs requests
func loggingMiddleware(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        start := time.Now()
        next.ServeHTTP(w, r)
        // Log: method, path, duration
    })
}

// corsMiddleware adds CORS headers
func corsMiddleware(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        w.Header().Set("Access-Control-Allow-Origin", "*")
        w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS")
        w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

        if r.Method == "OPTIONS" {
            w.WriteHeader(http.StatusOK)
            return
        }
        next.ServeHTTP(w, r)
    })
}

// privateSubnetMiddleware restricts access to private IPs
func privateSubnetMiddleware(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        ip := extractIP(r)
        if !isPrivateIP(ip) {
            http.Error(w, "Forbidden", http.StatusForbidden)
            return
        }
        next.ServeHTTP(w, r)
    })
}

func extractIP(r *http.Request) net.IP {
    // Extract IP from X-Forwarded-For or RemoteAddr
    // Reuse logic from src/internal/api/middleware.go
    return nil // TODO
}

func isPrivateIP(ip net.IP) bool {
    // Check if IP is in private ranges
    // 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8
    return false // TODO
}
```

**Reference files:**
- `src/internal/api/middleware.go` - Existing middleware logic

---

## Phase 4: IPC Transport Layer

### Step 4.1: Create IPC Protocol (`src/pkg/transport/ipc/protocol.go`)

**Purpose:** Simple JSON-based protocol over Unix socket.

```go
package ipc

import "encoding/json"

// Request is sent by CLI to daemon
type Request struct {
    Command string          `json:"command"` // e.g., "status", "create_list"
    Payload json.RawMessage `json:"payload"` // JSON-encoded request
}

// Response is sent by daemon to CLI
type Response struct {
    Data  interface{} `json:"data,omitempty"`
    Error *ErrorInfo  `json:"error,omitempty"`
}

type ErrorInfo struct {
    Code    string `json:"code"`
    Message string `json:"message"`
}
```

### Step 4.2: Create IPC Server (`src/pkg/transport/ipc/server.go`)

**Purpose:** Unix socket listener that calls controller methods.

```go
package ipc

import (
    "context"
    "encoding/json"
    "net"
    "os"
    "github.com/maksimkurb/keen-pbr/src/pkg/controller"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
)

const DefaultSocketPath = "/tmp/keen-pbr.sock"

// Server listens on Unix socket
type Server struct {
    ctrl       controller.AppController
    socketPath string
    listener   net.Listener
}

// NewServer creates an IPC server
func NewServer(ctrl controller.AppController, socketPath string) *Server {
    return &Server{
        ctrl:       ctrl,
        socketPath: socketPath,
    }
}

// Start begins listening on the socket
func (s *Server) Start() error {
    // Remove existing socket
    os.Remove(s.socketPath)

    listener, err := net.Listen("unix", s.socketPath)
    if err != nil {
        return err
    }
    s.listener = listener

    // Set permissions (0660)
    os.Chmod(s.socketPath, 0660)

    go s.acceptConnections()
    return nil
}

func (s *Server) acceptConnections() {
    for {
        conn, err := s.listener.Accept()
        if err != nil {
            return // Listener closed
        }
        go s.handleConnection(conn)
    }
}

func (s *Server) handleConnection(conn net.Conn) {
    defer conn.Close()

    var req Request
    if err := json.NewDecoder(conn).Decode(&req); err != nil {
        writeResponse(conn, nil, err)
        return
    }

    // Route command to controller method
    data, err := s.routeCommand(req.Command, req.Payload)
    writeResponse(conn, data, err)
}

func (s *Server) routeCommand(command string, payload json.RawMessage) (interface{}, error) {
    ctx := context.Background()

    switch command {
    case "status":
        return s.ctrl.GetStatus(ctx)

    case "interfaces":
        return s.ctrl.GetInterfaces(ctx)

    case "dns_servers":
        return s.ctrl.GetDNSServers(ctx)

    case "list_lists":
        return s.ctrl.ListLists(ctx)

    case "create_list":
        var req api.CreateListRequest
        if err := json.Unmarshal(payload, &req); err != nil {
            return nil, api.NewValidationError("invalid payload")
        }
        return nil, s.ctrl.CreateList(ctx, &req)

    // ... map all other commands

    default:
        return nil, api.NewValidationError("unknown command: " + command)
    }
}

func writeResponse(conn net.Conn, data interface{}, err error) {
    resp := Response{Data: data}
    if err != nil {
        if appErr, ok := err.(*api.AppError); ok {
            resp.Error = &ErrorInfo{Code: appErr.Code, Message: appErr.Message}
        } else {
            resp.Error = &ErrorInfo{Code: "INTERNAL_ERROR", Message: err.Error()}
        }
    }
    json.NewEncoder(conn).Encode(resp)
}

// Stop closes the listener
func (s *Server) Stop() error {
    if s.listener != nil {
        s.listener.Close()
    }
    os.Remove(s.socketPath)
    return nil
}
```

### Step 4.3: Create IPC Client (`src/pkg/transport/ipc/client.go`)

**Purpose:** CLI uses this to communicate with daemon.

```go
package ipc

import (
    "encoding/json"
    "net"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
)

// Client sends commands to daemon over Unix socket
type Client struct {
    socketPath string
}

// NewClient creates an IPC client
func NewClient(socketPath string) *Client {
    return &Client{socketPath: socketPath}
}

// SendCommand sends a command and returns the response
func (c *Client) SendCommand(command string, payload interface{}) (interface{}, error) {
    conn, err := net.Dial("unix", c.socketPath)
    if err != nil {
        return nil, err
    }
    defer conn.Close()

    // Encode payload
    payloadJSON, err := json.Marshal(payload)
    if err != nil {
        return nil, err
    }

    // Send request
    req := Request{Command: command, Payload: payloadJSON}
    if err := json.NewEncoder(conn).Encode(req); err != nil {
        return nil, err
    }

    // Read response
    var resp Response
    if err := json.NewDecoder(conn).Decode(&resp); err != nil {
        return nil, err
    }

    if resp.Error != nil {
        return nil, &api.AppError{
            Code:    resp.Error.Code,
            Message: resp.Error.Message,
        }
    }

    return resp.Data, nil
}

// IsDaemonRunning checks if daemon is reachable
func IsDaemonRunning(socketPath string) bool {
    conn, err := net.Dial("unix", socketPath)
    if err != nil {
        return false
    }
    conn.Close()
    return true
}
```

---

## Phase 5: CLI Refactor

### Step 5.1: Update Main Entry Point (`src/cmd/keen-pbr/main.go`)

**Purpose:** CLI decides whether to use IPC or direct controller.

```go
package main

import (
    "fmt"
    "os"
    "github.com/maksimkurb/keen-pbr/src/pkg/transport/ipc"
    "github.com/maksimkurb/keen-pbr/src/pkg/controller"
    "github.com/maksimkurb/keen-pbr/src/internal/core"
)

const socketPath = "/tmp/keen-pbr.sock"

func main() {
    if len(os.Args) < 2 {
        printUsage()
        os.Exit(1)
    }

    command := os.Args[1]

    // Special case: service command (daemon mode)
    if command == "service" {
        runDaemon()
        return
    }

    // Try IPC first (if daemon is running)
    if ipc.IsDaemonRunning(socketPath) {
        if err := runViaIPC(command); err != nil {
            fmt.Fprintf(os.Stderr, "Error: %v\n", err)
            os.Exit(1)
        }
        return
    }

    // Fallback: Run directly (daemon not running)
    if err := runDirect(command); err != nil {
        fmt.Fprintf(os.Stderr, "Error: %v\n", err)
        os.Exit(1)
    }
}

// runDaemon starts the service (HTTP + IPC servers)
func runDaemon() {
    // Create controller (reuses existing internal/ packages)
    deps := core.NewDefaultDependencies()
    ctrl := controller.NewService(deps)

    // Start IPC server
    ipcServer := ipc.NewServer(ctrl, socketPath)
    if err := ipcServer.Start(); err != nil {
        fmt.Fprintf(os.Stderr, "Failed to start IPC server: %v\n", err)
        os.Exit(1)
    }
    defer ipcServer.Stop()

    // Start HTTP server (optional, based on --api flag)
    if shouldStartAPI() {
        httpServer := http.NewServer(ctrl)
        go func() {
            http.ListenAndServe(":8080", httpServer)
        }()
    }

    // Wait for signals...
    waitForShutdown()
}

// runViaIPC sends command to running daemon
func runViaIPC(command string) error {
    client := ipc.NewClient(socketPath)

    switch command {
    case "status":
        resp, err := client.SendCommand("status", nil)
        if err != nil {
            return err
        }
        printJSON(resp)

    case "download":
        // Parse args, send download command
        resp, err := client.SendCommand("download_lists", parseDownloadArgs())
        if err != nil {
            return err
        }
        printJSON(resp)

    // ... handle other commands
    }

    return nil
}

// runDirect runs command without daemon (creates controller directly)
func runDirect(command string) error {
    // Create controller (same as daemon)
    deps := core.NewDefaultDependencies()
    ctrl := controller.NewService(deps)

    switch command {
    case "status":
        resp, err := ctrl.GetStatus(context.Background())
        if err != nil {
            return err
        }
        printJSON(resp)

    case "download":
        names := parseDownloadArgs()
        resp, err := ctrl.DownloadLists(context.Background(), names)
        if err != nil {
            return err
        }
        printJSON(resp)

    // ... handle other commands
    }

    return nil
}
```

**Key points:**

1. **service command:** Starts both IPC and HTTP servers (daemon mode)
2. **Other commands:** Try IPC first, fallback to direct if daemon not running
3. **Reuses controller:** Same logic whether via IPC or direct

### Step 5.2: Create CLI Command Handlers (`src/cmd/keen-pbr/commands.go`)

**Purpose:** Parse CLI args and map to controller/IPC calls.

```go
package main

import (
    "context"
    "flag"
    "github.com/maksimkurb/keen-pbr/src/pkg/api"
)

// Command handlers parse args and call controller methods

func handleDownloadCommand(args []string, ctrl controller.AppController) error {
    fs := flag.NewFlagSet("download", flag.ExitOnError)
    all := fs.Bool("all", false, "Download all lists")
    fs.Parse(args)

    var names []string
    if !*all {
        names = fs.Args() // Specific list names
    }

    result, err := ctrl.DownloadLists(context.Background(), names)
    if err != nil {
        return err
    }

    printJSON(result)
    return nil
}

func handleInterfacesCommand(ctrl controller.AppController) error {
    interfaces, err := ctrl.GetInterfaces(context.Background())
    if err != nil {
        return err
    }

    printJSON(interfaces)
    return nil
}

func handleDNSCommand(ctrl controller.AppController) error {
    servers, err := ctrl.GetDNSServers(context.Background())
    if err != nil {
        return err
    }

    printJSON(servers)
    return nil
}

// ... other command handlers
```

**Reference files:**
- `src/internal/commands/*.go` - Existing CLI command logic

---

## Phase 6: Integration & Testing

### Step 6.1: Wire Everything Together

**Update `src/cmd/keen-pbr/main.go`:**

1. Import new packages from `src/pkg/`
2. Create controller with dependencies from `src/internal/core`
3. Start IPC server (always)
4. Start HTTP server (if --api flag)
5. Keep existing signal handling and monitoring

### Step 6.2: Compatibility Layer (Temporary)

**Purpose:** During migration, allow both old and new systems to coexist.

Create `src/pkg/compat/bridge.go`:

```go
package compat

import (
    "github.com/maksimkurb/keen-pbr/src/internal/api"
    "github.com/maksimkurb/keen-pbr/src/pkg/controller"
)

// BridgeController wraps old API handlers with new controller interface
// This allows gradual migration
type BridgeController struct {
    oldHandler *api.Handler
}

// Use old handler as fallback during migration
func (b *BridgeController) GetStatus(ctx context.Context) (*api.StatusResponse, error) {
    // Temporarily delegate to old handler
    // Replace with new implementation incrementally
    return b.oldHandler.GetStatusData()
}
```

**Migration strategy:**

1. Start with compatibility bridge
2. Implement one controller method at a time
3. Replace bridge delegation with real implementation
4. Test each endpoint before moving to next
5. Remove bridge when all methods implemented

### Step 6.3: Testing Checklist

For each endpoint/command:

- [ ] Test via HTTP API (curl/Postman)
- [ ] Test via CLI (daemon running - uses IPC)
- [ ] Test via CLI (daemon not running - direct mode)
- [ ] Verify same response format as old API
- [ ] Check memory usage (no regression)
- [ ] Verify SSE streaming works (for check endpoints)

**Memory testing:**

```bash
# Before: run old implementation
keen-pbr service &
PID=$!
watch -n 1 'ps -o pid,vsz,rss,comm -p '$PID

# After: run new implementation
# Compare VSZ (virtual) and RSS (resident) memory
```

### Step 6.4: Performance Benchmarks

Create `src/pkg/controller/benchmark_test.go`:

```go
package controller_test

import (
    "testing"
    "github.com/maksimkurb/keen-pbr/src/pkg/controller"
)

func BenchmarkGetStatus(b *testing.B) {
    ctrl := setupController()
    ctx := context.Background()

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        ctrl.GetStatus(ctx)
    }
}

// Benchmark memory allocations
func BenchmarkGetStatusAllocs(b *testing.B) {
    ctrl := setupController()
    ctx := context.Background()

    b.ReportAllocs()
    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        ctrl.GetStatus(ctx)
    }
}
```

**Run benchmarks:**
```bash
go test -bench=. -benchmem ./src/pkg/controller/
```

**Target metrics:**

- Allocations per operation: < 10
- Bytes allocated per op: < 5KB
- Response time: < 10ms for simple operations

---

## Phase 7: Optimization

### Step 7.1: Config Caching

**Problem:** Config loaded from disk on every request.

**Solution:** Cache config with file watcher.

Create `src/pkg/controller/config_cache.go`:

```go
package controller

import (
    "sync"
    "time"
    "github.com/maksimkurb/keen-pbr/src/internal/config"
)

// ConfigCache caches config in memory with TTL
type ConfigCache struct {
    mu         sync.RWMutex
    cfg        *config.Config
    loadedAt   time.Time
    ttl        time.Duration
    configPath string
}

func NewConfigCache(path string, ttl time.Duration) *ConfigCache {
    return &ConfigCache{
        configPath: path,
        ttl:        ttl,
    }
}

func (c *ConfigCache) Get() (*config.Config, error) {
    c.mu.RLock()
    if c.cfg != nil && time.Since(c.loadedAt) < c.ttl {
        cfg := c.cfg
        c.mu.RUnlock()
        return cfg, nil
    }
    c.mu.RUnlock()

    // Reload config
    c.mu.Lock()
    defer c.mu.Unlock()

    cfg, err := config.Load(c.configPath)
    if err != nil {
        return nil, err
    }

    c.cfg = cfg
    c.loadedAt = time.Now()
    return cfg, nil
}

func (c *ConfigCache) Invalidate() {
    c.mu.Lock()
    c.cfg = nil
    c.mu.Unlock()
}
```

**Integration:**

- Use in controller methods: `cfg, err := s.configCache.Get()`
- Invalidate on config changes: `s.configCache.Invalidate()`
- Set TTL to 5 seconds (balance between freshness and performance)

### Step 7.2: DNS Server Concurrency Limiter

**Problem:** DNS server can spawn unlimited goroutines under load.

**Solution:** Semaphore pattern (from PLAN.md).

Update `src/internal/dnsproxy/server.go` (if needed) or wrap in controller:

```go
package controller

var dnsQuerySemaphore = make(chan struct{}, 50) // Max 50 concurrent queries

func (s *Service) handleDNSQuery(query []byte) {
    dnsQuerySemaphore <- struct{}{} // Acquire
    defer func() { <-dnsQuerySemaphore }() // Release

    // Process query...
}
```

### Step 7.3: Response Size Monitoring

**Purpose:** Track response sizes to detect memory leaks.

Add to `src/pkg/transport/http/middleware.go`:

```go
func responseSizeMiddleware(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        rw := &responseWriter{ResponseWriter: w}
        next.ServeHTTP(rw, r)

        // Log large responses (> 1MB)
        if rw.size > 1024*1024 {
            log.Printf("Large response: %s %s = %d bytes", r.Method, r.URL.Path, rw.size)
        }
    })
}

type responseWriter struct {
    http.ResponseWriter
    size int
}

func (rw *responseWriter) Write(b []byte) (int, error) {
    n, err := rw.ResponseWriter.Write(b)
    rw.size += n
    return n, err
}
```

---

## Phase 8: Documentation

### Step 8.1: Update Architecture Docs

Update `.claude/CONTEXT.md`:

1. Document new `src/pkg/` structure
2. Explain unified controller pattern
3. Update architecture diagrams
4. Document IPC protocol

### Step 8.2: API Documentation

Generate OpenAPI spec from code:

```go
// src/pkg/transport/http/openapi.go
// Auto-generate OpenAPI 3.0 spec from routes
```

### Step 8.3: Developer Guide

Create `DEVELOPMENT.md`:

1. How to add new endpoints
2. Controller method pattern
3. Testing guidelines
4. Memory optimization tips

---

## Migration Checklist

### Prerequisites
- [ ] Go 1.22+ installed (for pattern matching in ServeMux)
- [ ] All existing tests passing
- [ ] Baseline memory measurements recorded

### Phase 1: Foundation
- [ ] Create `src/pkg/api/` with all types
- [ ] Create `src/pkg/pool/` with buffer pools
- [ ] Create error types

### Phase 2: Controller
- [ ] Create controller interface
- [ ] Implement service struct
- [ ] Extract status methods
- [ ] Extract lists methods
- [ ] Extract ipsets methods
- [ ] Extract settings methods
- [ ] Extract service control methods
- [ ] Extract check methods (with SSE)
- [ ] Test each method in isolation

### Phase 3: HTTP Transport
- [ ] Create HTTP server with stdlib
- [ ] Implement generic handlers
- [ ] Implement all route handlers
- [ ] Implement middleware
- [ ] Test all endpoints with curl
- [ ] Verify SSE streaming

### Phase 4: IPC Transport
- [ ] Create IPC protocol
- [ ] Create IPC server
- [ ] Create IPC client
- [ ] Test client-server communication

### Phase 5: CLI
- [ ] Update main.go entry point
- [ ] Implement daemon mode
- [ ] Implement IPC mode
- [ ] Implement direct mode
- [ ] Test all CLI commands

### Phase 6: Integration
- [ ] Wire all components
- [ ] Run integration tests
- [ ] Memory profiling
- [ ] Performance benchmarks
- [ ] Frontend compatibility check

### Phase 7: Optimization
- [ ] Implement config caching
- [ ] Add DNS concurrency limiter
- [ ] Add response size monitoring
- [ ] Run benchmarks again

### Phase 8: Cleanup
- [ ] Remove compatibility bridge
- [ ] Update documentation
- [ ] Remove unused imports
- [ ] Final testing round

### Phase 9: Deployment
- [ ] Build for all architectures
- [ ] Package IPK files
- [ ] Update VERSION
- [ ] Create release notes
- [ ] Tag release

---

## Success Metrics

### Functional
- ✅ All 35+ API endpoints work identically
- ✅ All CLI commands work
- ✅ IPC communication works
- ✅ SSE streaming works
- ✅ Frontend works without changes

### Performance
- ✅ Memory usage: < 15MB (target for 128MB router)
- ✅ Request latency: < 10ms for simple operations
- ✅ Binary size: < 20MB (MIPS architecture)
- ✅ DNS query latency: < 5ms (cached)

### Code Quality
- ✅ Zero modifications to `src/internal/`
- ✅ DRY: Business logic in one place (controller)
- ✅ KISS: Simple, readable code
- ✅ Well-documented packages
- ✅ Comprehensive tests

---

## Rollback Plan

If migration fails:

1. Revert to `src/internal/` code (untouched)
2. Remove `src/pkg/` directory
3. Restore old `src/cmd/keen-pbr/main.go`
4. No data loss (config files unchanged)

---

## Timeline Estimate

| Phase | Effort | Description |
|-------|--------|-------------|
| 1. Foundation | 2-4 hours | Create types, pools, errors |
| 2. Controller | 8-12 hours | Extract all business logic |
| 3. HTTP Transport | 4-6 hours | Stdlib HTTP server |
| 4. IPC Transport | 3-4 hours | Unix socket server/client |
| 5. CLI | 3-4 hours | Update main.go, commands |
| 6. Integration | 4-6 hours | Testing, debugging |
| 7. Optimization | 2-3 hours | Caching, limiting |
| 8. Docs | 2-3 hours | Update documentation |
| **Total** | **28-42 hours** | ~1-2 weeks for one developer |

---

## Questions to Clarify

Before starting implementation:

1. **Backward Compatibility:** Do we need to support both old and new HTTP APIs simultaneously during transition?
2. **IPC Protocol:** Is JSON over Unix socket acceptable, or prefer binary protocol (protobuf/msgpack)?
3. **Config Migration:** Should we auto-migrate old config format, or require manual update?
4. **Frontend:** Will frontend need updates, or must it work unchanged with new backend?
5. **Testing:** Do we have existing integration tests to verify behavior parity?
6. **Deployment:** Can we do rolling update, or requires full restart?

---

## Next Steps

1. **Review this plan** - Confirm approach aligns with vision
2. **Answer clarification questions** - Resolve ambiguities
3. **Set up development branch** - `feature/unified-controller`
4. **Begin Phase 1** - Start with foundation (types, pools)
5. **Iterate incrementally** - Test after each phase

---

**End of Refactoring Plan**
