package api

import (
	"net/http"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/frontend"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
)

// NewRouter creates a new HTTP router with all API endpoints.
func NewRouter(configPath string, deps *domain.AppDependencies, serviceMgr ServiceManager) http.Handler {
	r := chi.NewRouter()

	// Apply middleware
	r.Use(Recovery)
	r.Use(Logger)
	r.Use(PrivateSubnetOnly)  // Restrict access to private subnets
	r.Use(CORS)
	r.Use(JSONContentType)

	// Create handler
	h := NewHandler(configPath, deps, serviceMgr)

	// API v1 routes
	r.Route("/api/v1", func(r chi.Router) {
		// Lists endpoints
		r.Get("/lists", h.GetLists)
		r.Post("/lists", h.CreateList)
		r.Get("/lists/{name}", h.GetList)
		r.Put("/lists/{name}", h.UpdateList)
		r.Delete("/lists/{name}", h.DeleteList)

		// IPSets endpoints
		r.Get("/ipsets", h.GetIPSets)
		r.Post("/ipsets", h.CreateIPSet)
		r.Get("/ipsets/{name}", h.GetIPSet)
		r.Put("/ipsets/{name}", h.UpdateIPSet)
		r.Delete("/ipsets/{name}", h.DeleteIPSet)

		// Settings endpoints
		r.Get("/settings", h.GetSettings)
		r.Patch("/settings", h.UpdateSettings)

		// Status endpoint
		r.Get("/status", h.GetStatus)

		// Service control endpoints
		r.Post("/service", h.ControlService)
		r.Post("/dnsmasq", h.ControlDnsmasq)

		// Health check endpoint
		r.Get("/health", h.CheckHealth)

		// Network check endpoints
		r.Post("/check/routing", h.CheckRouting)
		r.Post("/check/ping", h.CheckPing)
		r.Post("/check/traceroute", h.CheckTraceroute)
	})

	// Serve static frontend files
	if staticFS, err := frontend.GetHTTPFileSystem(); err == nil {
		fileServer := http.FileServer(staticFS)
		r.Handle("/*", fileServer)
	}

	return r
}
