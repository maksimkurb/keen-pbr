package commands

import (
	"context"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/api"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/dnscheck"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ServerCommand implements the server command for running the HTTP API server.
type ServerCommand struct {
	fs   *flag.FlagSet
	ctx  *AppContext
	cfg  *config.Config
	deps *domain.AppDependencies

	// Command-specific flags
	bindAddr        string
	monitorInterval int

	// Service manager for controlling the routing service
	serviceMgr       *ServiceManager
	configHasher     *config.ConfigHasher
	dnsCheckListener *dnscheck.DNSCheckListener
}

// CreateServerCommand creates a new server command.
func CreateServerCommand() Runner {
	return &ServerCommand{}
}

// Name returns the command name.
func (c *ServerCommand) Name() string {
	return "server"
}

// Init initializes the server command with arguments.
func (c *ServerCommand) Init(args []string, ctx *AppContext) error {
	c.ctx = ctx
	c.fs = flag.NewFlagSet("server", flag.ExitOnError)

	// Define command-specific flags
	c.fs.StringVar(&c.bindAddr, "bind", "0.0.0.0:8080", "Address to bind the HTTP server (e.g., 0.0.0.0:8080)")
	c.fs.IntVar(&c.monitorInterval, "monitor-interval", 10, "Interval in seconds to monitor interface changes")

	// Parse flags
	if err := c.fs.Parse(args); err != nil {
		return err
	}

	// Load configuration (but allow starting even if validation fails)
	// Interface validation will be done when actually starting the service
	cfg, err := config.LoadConfig(ctx.ConfigPath)
	if err != nil {
		return fmt.Errorf("failed to load configuration file: %w", err)
	}

	// Validate config structure (but not runtime requirements like interfaces)
	if err := cfg.ValidateConfig(); err != nil {
		log.Warnf("Configuration validation failed: %v", err)
		log.Warnf("Server will start, but service cannot be started until config is fixed")
	}
	c.cfg = cfg

	// Use config value if provided, otherwise use flag value
	if cfg.General.APIBindAddress != "" {
		c.bindAddr = cfg.General.APIBindAddress
	}

	// Create dependencies
	c.deps = domain.NewDefaultDependencies()

	// Create ConfigHasher DI component
	c.configHasher = config.NewConfigHasher(ctx.ConfigPath)

	// Set Keenetic client on ConfigHasher for DNS server tracking
	c.configHasher.SetKeeneticClient(c.deps.KeeneticClient())

	// Create service manager
	serviceMgr, err := NewServiceManager(ctx, c.monitorInterval, c.configHasher)
	if err != nil {
		return fmt.Errorf("failed to create service manager: %w", err)
	}
	c.serviceMgr = serviceMgr

	return nil
}

// Run starts the HTTP API server.
func (c *ServerCommand) Run() error {
	log.Infof("Starting keen-pbr API server on %s", c.bindAddr)
	log.Infof("Configuration loaded from: %s", c.ctx.ConfigPath)
	log.Infof("")
	log.Infof("Access restricted to private subnets only:")
	log.Infof("  IPv4: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8")
	log.Infof("  IPv6: fc00::/7, fe80::/10, ::1/128")
	log.Infof("Requests from public IPs will be rejected with 403 Forbidden")
	log.Infof("")

	// Try to start the routing service (but don't fail if it can't start)
	// The user can see the error in the UI and fix the config
	if err := c.serviceMgr.Start(); err != nil {
		log.Errorf("Failed to start routing service: %v", err)
		log.Warnf("API server will start anyway. Fix the configuration and try starting the service again.")
	}

	// Start DNS check listener
	dnsCheckPort := c.cfg.General.GetDNSCheckPort()
	c.dnsCheckListener = dnscheck.NewDNSCheckListener(dnsCheckPort)
	if err := c.dnsCheckListener.Start(); err != nil {
		log.Warnf("Failed to start DNS check listener: %v", err)
		log.Warnf("DNS check feature will not be available")
		c.dnsCheckListener = nil // Set to nil so API handler knows it's not available
	}

	// Create router with service manager, config hasher, and DNS check listener
	router := api.NewRouter(c.ctx.ConfigPath, c.deps, c.serviceMgr, c.configHasher, c.dnsCheckListener)

	// Create HTTP server
	server := &http.Server{
		Addr:         c.bindAddr,
		Handler:      router,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// Channel to listen for errors from the server
	serverErrors := make(chan error, 1)

	// Start server in a goroutine
	go func() {
		log.Infof("API server listening on http://%s", c.bindAddr)
		log.Infof("API endpoints available at http://%s/api/v1", c.bindAddr)
		serverErrors <- server.ListenAndServe()
	}()

	// Channels to listen for signals
	shutdown := make(chan os.Signal, 1)
	reload := make(chan os.Signal, 1)
	signal.Notify(shutdown, os.Interrupt, syscall.SIGTERM, syscall.SIGINT)
	signal.Notify(reload, syscall.SIGHUP)

	// Run signal handling loop
	for {
		select {
		case err := <-serverErrors:
			if err != nil && err != http.ErrServerClosed {
				// Stop the service before returning
				if stopErr := c.serviceMgr.Stop(); stopErr != nil {
					log.Errorf("Failed to stop service: %v", stopErr)
				}
				return fmt.Errorf("server error: %w", err)
			}
			return nil

		case <-reload:
			log.Infof("Received SIGHUP signal, reloading configuration...")
			if err := c.serviceMgr.Reload(); err != nil {
				log.Errorf("Failed to reload configuration: %v", err)
			} else {
				log.Infof("Configuration reloaded successfully")
			}

		case sig := <-shutdown:
			log.Infof("Received signal %v, shutting down server...", sig)

			// Stop the service first
			log.Infof("Stopping routing service...")
			if err := c.serviceMgr.Stop(); err != nil {
				log.Errorf("Failed to stop service: %v", err)
			}

			// Stop DNS check listener if it was started
			if c.dnsCheckListener != nil {
				log.Infof("Stopping DNS check listener...")
				if err := c.dnsCheckListener.Stop(); err != nil {
					log.Errorf("Failed to stop DNS check listener: %v", err)
				}
			}

			// Create context with timeout for shutdown
			ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
			defer cancel()

			// Attempt graceful shutdown
			if err := server.Shutdown(ctx); err != nil {
				log.Errorf("Error during server shutdown: %v", err)
				// Force close if graceful shutdown fails
				if err := server.Close(); err != nil {
					return fmt.Errorf("failed to close server: %w", err)
				}
				return fmt.Errorf("server shutdown failed: %w", err)
			}

			log.Infof("Server stopped gracefully")
			return nil
		}
	}
}
