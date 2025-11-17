package api

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/networking"
)

// Server represents the API server with integrated routing service
type Server struct {
	router         *chi.Mux
	httpServer     *http.Server
	configPath     string
	routingService *RoutingService
}

// NewServer creates a new API server with integrated routing service
func NewServer(configPath string, bindAddr string, interfaces []networking.Interface) *Server {
	s := &Server{
		configPath:     configPath,
		router:         chi.NewRouter(),
		routingService: NewRoutingService(configPath, interfaces),
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

// GetRoutingService returns the routing service instance
func (s *Server) GetRoutingService() *RoutingService {
	return s.routingService
}

// setupRoutes configures all API routes
func (s *Server) setupRoutes() {
	// API v1 routes
	s.router.Route("/api/v1", func(r chi.Router) {
		// Lists management
		r.Route("/lists", func(r chi.Router) {
			r.Get("/", HandleListsList(s.configPath))
			r.Post("/", HandleListsCreate(s.configPath, s.routingService))
			r.Get("/{list_name}", HandleListsGet(s.configPath))
			r.Put("/{list_name}", HandleListsUpdate(s.configPath, s.routingService))
			r.Delete("/{list_name}", HandleListsDelete(s.configPath, s.routingService))
		})

		// IPSets management
		r.Route("/ipsets", func(r chi.Router) {
			r.Get("/", HandleIPSetsList(s.configPath))
			r.Post("/", HandleIPSetsCreate(s.configPath, s.routingService))
			r.Get("/{ipset_name}", HandleIPSetsGet(s.configPath))
			r.Put("/{ipset_name}", HandleIPSetsUpdate(s.configPath, s.routingService))
			r.Delete("/{ipset_name}", HandleIPSetsDelete(s.configPath, s.routingService))
		})

		// General settings
		r.Route("/general", func(r chi.Router) {
			r.Get("/", HandleGeneralGet(s.configPath))
			r.Post("/", HandleGeneralUpdate(s.configPath, s.routingService))
		})

		// Status
		r.Get("/status", HandleStatus(s.configPath, s.routingService))

		// Service control
		r.Post("/service", HandleServiceControl(s.routingService))

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

// Start starts the API server and routing service
func (s *Server) Start() error {
	log.Infof("[API] Starting keen-pbr integrated server")
	log.Infof("[API] Config file: %s", s.configPath)
	log.Infof("[API] API bind address: %s", s.httpServer.Addr)
	log.Infof("[API] API documentation: .claude/REST.md")
	log.Infof("[API] Example: curl http://%s/api/v1/status", s.httpServer.Addr)

	// Start routing service
	if err := s.routingService.Start(); err != nil {
		return fmt.Errorf("failed to start routing service: %w", err)
	}

	// Start HTTP server
	if err := s.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		return fmt.Errorf("server error: %w", err)
	}

	return nil
}

// Stop gracefully stops the API server and routing service
func (s *Server) Stop(ctx context.Context) error {
	log.Infof("[API] Shutting down integrated server...")

	// Stop routing service first
	if err := s.routingService.Stop(); err != nil {
		log.Warnf("[API] Failed to stop routing service: %v", err)
	}

	// Then stop HTTP server
	return s.httpServer.Shutdown(ctx)
}
