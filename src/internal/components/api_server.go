package components

import (
	"context"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/api"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/core"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// DNSProxyProvider provides access to the DNS proxy instance
type DNSProxyProvider interface {
	GetDNSProxy() *dnsproxy.DNSProxy
}

// APIServer manages the HTTP API server
type APIServer struct {
	bindAddr     string
	configPath   string
	deps         *core.AppDependencies
	serviceMgr   api.ServiceManager
	configHasher *config.ConfigHasher
	dnsProvider  DNSProxyProvider
	httpServer   *http.Server
	running      bool
	mu           sync.Mutex
	shutdownChan chan struct{}
}

// NewAPIServer creates a new API server component
func NewAPIServer(
	bindAddr string,
	configPath string,
	deps *core.AppDependencies,
	serviceMgr api.ServiceManager,
	configHasher *config.ConfigHasher,
	dnsProvider DNSProxyProvider,
) *APIServer {
	return &APIServer{
		bindAddr:     bindAddr,
		configPath:   configPath,
		deps:         deps,
		serviceMgr:   serviceMgr,
		configHasher: configHasher,
		dnsProvider:  dnsProvider,
		shutdownChan: make(chan struct{}),
	}
}

// Start starts the API server
func (a *APIServer) Start() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	if a.running {
		return fmt.Errorf("API server is already running")
	}

	log.Infof("Starting keen-pbr API server on %s", a.bindAddr)

	// Get DNS proxy from provider (may be nil if not started)
	var dnsproxy *dnsproxy.DNSProxy
	if a.dnsProvider != nil {
		dnsproxy = a.dnsProvider.GetDNSProxy()
	}

	// Create router with service manager
	router := api.NewRouter(
		a.configPath,
		a.deps,
		a.serviceMgr,
		a.configHasher,
		dnsproxy,
	)

	// Create HTTP server
	a.httpServer = &http.Server{
		Addr:         a.bindAddr,
		Handler:      router,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// Start server in goroutine
	go func() {
		if err := a.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Errorf("API server error: %v", err)
		}
	}()

	a.running = true
	return nil
}

// Stop stops the API server
func (a *APIServer) Stop() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	if !a.running {
		return fmt.Errorf("API server is not running")
	}

	log.Infof("Stopping API server...")

	// Shutdown HTTP server
	if a.httpServer != nil {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := a.httpServer.Shutdown(ctx); err != nil {
			log.Errorf("Error shutting down HTTP server: %v", err)
		}
	}

	a.running = false
	log.Infof("API server stopped")
	return nil
}

// IsRunning returns whether the API server is running
func (a *APIServer) IsRunning() bool {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.running
}
