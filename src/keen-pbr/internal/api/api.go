package api

import (
	"encoding/json"
	"net/http"
	"strings"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/config"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/service"
)

// Server represents the API server
type Server struct {
	config     *config.Config
	configPath string
	service    *service.Service
	mux        *http.ServeMux
}

// New creates a new API server
func New(cfg *config.Config, configPath string, svc *service.Service) *Server {
	s := &Server{
		config:     cfg,
		configPath: configPath,
		service:    svc,
		mux:        http.NewServeMux(),
	}

	s.setupRoutes()
	return s
}

// setupRoutes sets up all API routes
func (s *Server) setupRoutes() {
	// Service endpoints
	s.mux.HandleFunc("/v1/service/", s.handleService)

	// Bulk endpoints (handle before individual endpoints to avoid conflicts)
	s.mux.HandleFunc("/v1/rules", s.handleRulesBulk)
	s.mux.HandleFunc("/v1/outbounds", s.handleOutboundsBulk)

	// Individual rule endpoints (keep GET only)
	s.mux.HandleFunc("/v1/rules/", s.handleRulesWithID)

	// Individual outbound endpoints (keep GET only)
	s.mux.HandleFunc("/v1/outbounds/", s.handleOutboundsWithTag)

	// Config endpoint
	s.mux.HandleFunc("/v1/config", s.handleConfig)

	// Info endpoints
	s.mux.HandleFunc("/v1/info/interfaces", s.handleInterfaces)

	// Serve static files for non-API routes
	s.mux.Handle("/", ServeStatic())
}

// ServeHTTP implements http.Handler
func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// CORS headers for API endpoints only
	if strings.HasPrefix(r.URL.Path, "/v1/") {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusOK)
			return
		}
	}

	s.mux.ServeHTTP(w, r)
}

// respondJSON sends a JSON response
func respondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

// respondError sends an error response
func respondError(w http.ResponseWriter, status int, message string) {
	respondJSON(w, status, map[string]string{"error": message})
}

// extractID extracts ID from URL path
func extractID(path, prefix string) string {
	return strings.TrimPrefix(path, prefix)
}
