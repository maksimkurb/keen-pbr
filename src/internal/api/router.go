package api

import (
	"net/http"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/frontend"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/dnscheck"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
)

// NewRouter creates a new HTTP router with all API endpoints.
func NewRouter(configPath string, deps *domain.AppDependencies, serviceMgr ServiceManager, configHasher *config.ConfigHasher, dnsCheckListener *dnscheck.DNSCheckListener) http.Handler {
	r := chi.NewRouter()

	// Apply middleware
	r.Use(Recovery)
	r.Use(Logger)
	r.Use(PrivateSubnetOnly)  // Restrict access to private subnets
	r.Use(CORS)
	r.Use(JSONContentType)

	// Create handler
	h := NewHandler(configPath, deps, serviceMgr, configHasher, dnsCheckListener)

	// API v1 routes
	r.Route("/api/v1", func(r chi.Router) {
		// Lists endpoints
		r.Get("/lists", h.GetLists)
		r.Post("/lists", h.CreateList)
		r.Get("/lists/{name}", h.GetList)
		r.Put("/lists/{name}", h.UpdateList)
		r.Delete("/lists/{name}", h.DeleteList)

		// List download endpoints
		r.Post("/lists-download", h.DownloadAllLists)
		r.Post("/lists-download/{name}", h.DownloadList)

		// IPSets endpoints
		r.Get("/ipsets", h.GetIPSets)
		r.Post("/ipsets", h.CreateIPSet)
		r.Get("/ipsets/{name}", h.GetIPSet)
		r.Put("/ipsets/{name}", h.UpdateIPSet)
		r.Delete("/ipsets/{name}", h.DeleteIPSet)

		// Settings endpoints
		r.Get("/settings", h.GetSettings)
		r.Patch("/settings", h.UpdateSettings)

		// Interfaces endpoint
		r.Get("/interfaces", h.GetInterfaces)

		// Status endpoint
		r.Get("/status", h.GetStatus)

		// Service control endpoints
		r.Post("/service", h.ControlService)
		r.Post("/dnsmasq", h.ControlDnsmasq)

		// Health check endpoint
		r.Get("/health", h.CheckHealth)

		// Network check endpoints
		r.Post("/check/routing", h.CheckRouting)
		r.Get("/check/ping", h.CheckPing)             // SSE stream
		r.Get("/check/traceroute", h.CheckTraceroute) // SSE stream
		r.Get("/check/self", h.CheckSelf)             // SSE stream
		r.Get("/check/split-dns", h.CheckSplitDNS)    // SSE stream
	})

	// Serve static frontend files
	if staticFS, err := frontend.GetHTTPFileSystem(); err == nil {
		fileServer := http.FileServer(staticFS)
		r.Handle("/*", fileServer)
	}

	return r
}
