package api

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// Server represents the API server
type Server struct {
	router     *chi.Mux
	httpServer *http.Server
	configPath string
}

// NewServer creates a new API server
func NewServer(configPath string, bindAddr string) *Server {
	s := &Server{
		configPath: configPath,
		router:     chi.NewRouter(),
	}

	// Setup middleware
	s.router.Use(RecoveryMiddleware)
	s.router.Use(LoggingMiddleware)
	s.router.Use(CORSMiddleware)
	s.router.Use(ContentTypeMiddleware)

	// Setup routes
	s.setupRoutes()

	// Create HTTP server
	s.httpServer = &http.Server{
		Addr:         bindAddr,
		Handler:      s.router,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	return s
}

// setupRoutes configures all API routes
func (s *Server) setupRoutes() {
	// API v1 routes
	s.router.Route("/api/v1", func(r chi.Router) {
		// Lists management
		r.Route("/lists", func(r chi.Router) {
			r.Get("/", HandleListsList(s.configPath))
			r.Post("/", HandleListsCreate(s.configPath))
			r.Get("/{list_name}", HandleListsGet(s.configPath))
			r.Put("/{list_name}", HandleListsUpdate(s.configPath))
			r.Delete("/{list_name}", HandleListsDelete(s.configPath))
		})

		// IPSets management
		r.Route("/ipsets", func(r chi.Router) {
			r.Get("/", HandleIPSetsList(s.configPath))
			r.Post("/", HandleIPSetsCreate(s.configPath))
			r.Get("/{ipset_name}", HandleIPSetsGet(s.configPath))
			r.Put("/{ipset_name}", HandleIPSetsUpdate(s.configPath))
			r.Delete("/{ipset_name}", HandleIPSetsDelete(s.configPath))
		})

		// General settings
		r.Route("/general", func(r chi.Router) {
			r.Get("/", HandleGeneralGet(s.configPath))
			r.Post("/", HandleGeneralUpdate(s.configPath))
		})

		// Status
		r.Get("/status", HandleStatus(s.configPath))

		// Service control
		r.Post("/service", HandleServiceControl(s.configPath))

		// Health checks
		r.Route("/check", func(r chi.Router) {
			r.Get("/networking", HandleCheckNetworking(s.configPath))
			r.Get("/ipset", HandleCheckIPSet(s.configPath))
		})
	})

	// Health check endpoint at root
	s.router.Get("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("OK"))
	})
}

// Start starts the API server
func (s *Server) Start() error {
	log.Infof("[API] Starting server on %s", s.httpServer.Addr)
	log.Infof("[API] API documentation: .claude/REST.md")
	log.Infof("[API] Example: curl http://%s/api/v1/status", s.httpServer.Addr)

	if err := s.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		return fmt.Errorf("server error: %w", err)
	}

	return nil
}

// Stop gracefully stops the API server
func (s *Server) Stop(ctx context.Context) error {
	log.Infof("[API] Shutting down server...")
	return s.httpServer.Shutdown(ctx)
}
