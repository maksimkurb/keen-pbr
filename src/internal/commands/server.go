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
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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
	serviceMgr   *ServiceManager
	configHasher *config.ConfigHasher

	// DNS proxy for domain-based routing
	dnsProxy    *dnsproxy.DNSProxy
	iptablesMgr networking.NetworkingComponent
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

	// DNS check subscriber - DNS proxy provides this functionality
	var dnsCheckSubscriber api.DNSCheckSubscriber

	// Start DNS proxy if enabled (it includes DNS check functionality)
	if c.cfg.General.IsDNSProxyEnabled() {
		if err := c.startDNSProxy(); err != nil {
			log.Errorf("Failed to start DNS proxy: %v", err)
			log.Warnf("Domain-based routing via DNS proxy will not be available")
			log.Warnf("DNS check feature will not be available")
		} else {
			// Use DNS proxy as the DNS check subscriber
			dnsCheckSubscriber = c.dnsProxy
			// Register callback to reload DNS proxy lists when lists are updated
			c.serviceMgr.SetOnListsUpdated(func() {
				if c.dnsProxy != nil {
					c.dnsProxy.ReloadLists()
				}
			})
		}
	} else {
		log.Infof("DNS proxy is disabled")
		log.Warnf("DNS check feature requires DNS proxy to be enabled")
	}

	// DNS servers provider - DNS proxy provides this functionality
	var dnsServersProvider api.DNSServersProvider
	if c.dnsProxy != nil {
		dnsServersProvider = c.dnsProxy
	}

	// Create router with service manager, config hasher, DNS check subscriber, and DNS servers provider
	router := api.NewRouter(c.ctx.ConfigPath, c.deps, c.serviceMgr, c.configHasher, dnsCheckSubscriber, dnsServersProvider)

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
	refreshRouting := make(chan os.Signal, 1)
	signal.Notify(shutdown, os.Interrupt, syscall.SIGTERM, syscall.SIGINT)
	signal.Notify(reload, syscall.SIGHUP)
	signal.Notify(refreshRouting, syscall.SIGUSR1)

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
			// Reload DNS proxy lists if DNS proxy is running
			if c.dnsProxy != nil {
				c.dnsProxy.ReloadLists()
			}

		case <-refreshRouting:
			log.Infof("Received SIGUSR1 signal, refreshing routing...")
			if err := c.serviceMgr.RefreshRouting(); err != nil {
				log.Errorf("Failed to refresh routing: %v", err)
			} else {
				log.Infof("Routing refreshed successfully")
			}

		case sig := <-shutdown:
			log.Infof("Received signal %v, shutting down server...", sig)

			// Stop the service first
			log.Infof("Stopping routing service...")
			if err := c.serviceMgr.Stop(); err != nil {
				log.Errorf("Failed to stop service: %v", err)
			}

			// Stop DNS proxy if it was started
			c.stopDNSProxy()

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

// startDNSProxy initializes and starts the DNS proxy.
func (c *ServerCommand) startDNSProxy() error {
	proxyCfg := dnsproxy.ProxyConfigFromAppConfig(c.cfg)

	// Create DNS proxy
	proxy, err := dnsproxy.NewDNSProxy(
		proxyCfg,
		c.deps.KeeneticClient(),
		c.deps.IPSetManager(),
		c.cfg,
	)
	if err != nil {
		return fmt.Errorf("failed to create DNS proxy: %w", err)
	}
	c.dnsProxy = proxy

	// Create iptables manager for DNS redirection
	iptablesMgr, err := networking.NewDNSRedirectComponent(proxyCfg.ListenAddress, proxyCfg.ListenAddressIPv6, proxyCfg.ListenPort)
	if err != nil {
		c.dnsProxy = nil
		return fmt.Errorf("failed to create iptables manager: %w", err)
	}
	c.iptablesMgr = iptablesMgr

	// Start the DNS proxy listener
	if err := c.dnsProxy.Start(); err != nil {
		c.dnsProxy = nil
		c.iptablesMgr = nil
		return fmt.Errorf("failed to start DNS proxy: %w", err)
	}

	// Enable iptables redirection
	if err := c.iptablesMgr.CreateIfNotExists(); err != nil {
		c.dnsProxy.Stop()
		c.dnsProxy = nil
		c.iptablesMgr = nil
		return fmt.Errorf("failed to enable DNS redirection: %w", err)
	}

	log.Infof("DNS proxy started on port %d with upstream %v", proxyCfg.ListenPort, proxyCfg.Upstreams)
	return nil
}

// stopDNSProxy stops the DNS proxy and removes iptables rules.
func (c *ServerCommand) stopDNSProxy() {
	if c.iptablesMgr != nil {
		log.Infof("Disabling DNS redirection...")
		if err := c.iptablesMgr.DeleteIfExists(); err != nil {
			log.Errorf("Failed to disable DNS redirection: %v", err)
		}
		c.iptablesMgr = nil
	}

	if c.dnsProxy != nil {
		log.Infof("Stopping DNS proxy...")
		if err := c.dnsProxy.Stop(); err != nil {
			log.Errorf("Failed to stop DNS proxy: %v", err)
		}
		c.dnsProxy = nil
	}
}
