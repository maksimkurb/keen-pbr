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
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

func CreateServiceCommand() *ServiceCommand {
	sc := &ServiceCommand{
		fs: flag.NewFlagSet("service", flag.ExitOnError),
	}

	sc.fs.IntVar(&sc.MonitorInterval, "monitor-interval", 10, "Interval in seconds to monitor interface changes")

	return sc
}

type ServiceCommand struct {
	fs              *flag.FlagSet
	cfg             *config.Config
	ctx             *AppContext
	MonitorInterval int

	// Dependencies
	deps         *domain.AppDependencies
	networkMgr   domain.NetworkManager
	configHasher *config.ConfigHasher

	// Service manager for controlling the routing service
	serviceMgr *ServiceManager

	// DNS proxy for domain-based routing
	// Note: DNS redirect iptables rules are managed by NetworkManager via global components
	dnsProxy *dnsproxy.DNSProxy

	// HTTP server
	httpServer *http.Server

	// Runner for crash isolation
	apiRunner *RestartableRunner
}

func (s *ServiceCommand) Name() string {
	return s.fs.Name()
}

func (s *ServiceCommand) Init(args []string, ctx *AppContext) error {
	s.ctx = ctx

	if err := s.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		s.cfg = cfg
	}

	if err := networking.ValidateInterfacesArePresent(s.cfg, ctx.Interfaces); err != nil {
		return fmt.Errorf("failed to validate interfaces: %v", err)
	}

	// Create dependencies
	s.deps = domain.NewDefaultDependencies()
	s.networkMgr = s.deps.NetworkManager()

	// Create ConfigHasher DI component
	s.configHasher = config.NewConfigHasher(ctx.ConfigPath)
	s.configHasher.SetKeeneticClient(s.deps.KeeneticClient())

	// Create service manager
	serviceMgr, err := NewServiceManager(ctx, s.MonitorInterval, s.configHasher)
	if err != nil {
		return fmt.Errorf("failed to create service manager: %w", err)
	}
	s.serviceMgr = serviceMgr

	return nil
}

func (s *ServiceCommand) Run() error {
	log.Infof("Starting keen-pbr service...")

	// Create context for graceful shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Setup signal handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP, syscall.SIGUSR1)

	// Start the routing service
	log.Infof("Starting routing service...")
	if err := s.serviceMgr.Start(); err != nil {
		log.Errorf("Failed to start routing service: %v", err)
		log.Warnf("Service will continue without routing. Fix the configuration and restart.")
	}

	// DNS check subscriber - DNS proxy provides this functionality
	var dnsCheckSubscriber api.DNSCheckSubscriber
	var dnsServersProvider api.DNSServersProvider

	// Start DNS proxy if enabled
	if s.cfg.General.IsDNSProxyEnabled() {
		if err := s.startDNSProxy(); err != nil {
			log.Errorf("Failed to start DNS proxy: %v", err)
			log.Warnf("Domain-based routing via DNS proxy will not be available")
		} else {
			dnsCheckSubscriber = s.dnsProxy
			dnsServersProvider = s.dnsProxy
			// Register callback to reload DNS proxy lists when lists are updated
			s.serviceMgr.SetOnListsUpdated(func() {
				if s.dnsProxy != nil {
					s.dnsProxy.ReloadLists()
				}
			})
		}
	} else {
		log.Infof("DNS proxy is disabled")
	}

	// Start HTTP API server if enabled
	if s.cfg.General.IsAPIEnabled() {
		bindAddr := s.cfg.General.GetAPIBindAddress()
		if err := s.startAPIServer(ctx, bindAddr, dnsCheckSubscriber, dnsServersProvider); err != nil {
			log.Errorf("Failed to start API server: %v", err)
			log.Warnf("Web UI will not be available")
		}
	} else {
		log.Infof("REST API and web UI is disabled")
	}

	log.Infof("Service started successfully.")
	log.Infof("Send SIGHUP to reload configuration, SIGUSR1 to refresh routing")

	// Run signal handling loop
	for sig := range sigChan {
		switch sig {
		case syscall.SIGHUP:
			log.Infof("Received SIGHUP signal, reloading configuration...")
			if err := s.serviceMgr.Reload(); err != nil {
				log.Errorf("Failed to reload configuration: %v", err)
			} else {
				log.Infof("Configuration reloaded successfully")
			}
			// Reload DNS proxy lists if DNS proxy is running
			if s.dnsProxy != nil {
				s.dnsProxy.ReloadLists()
			}

		case syscall.SIGUSR1:
			log.Infof("Received SIGUSR1 signal, refreshing routing...")
			if err := s.serviceMgr.RefreshRouting(); err != nil {
				log.Errorf("Failed to refresh routing: %v", err)
			} else {
				log.Infof("Routing refreshed successfully")
			}

		case syscall.SIGINT, syscall.SIGTERM:
			log.Infof("Received signal %v, shutting down...", sig)
			return s.shutdown()
		}
	}
	return nil
}

// startAPIServer starts the HTTP API server in a separate goroutine.
func (s *ServiceCommand) startAPIServer(ctx context.Context, bindAddr string, dnsCheckSubscriber api.DNSCheckSubscriber, dnsServersProvider api.DNSServersProvider) error {
	log.Infof("Starting keen-pbr API server on %s", bindAddr)
	log.Infof("")
	log.Infof("Access restricted to private subnets only:")
	log.Infof("  IPv4: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8")
	log.Infof("  IPv6: fc00::/7, fe80::/10, ::1/128")
	log.Infof("")

	// Create router with service manager
	router := api.NewRouter(s.ctx.ConfigPath, s.deps, s.serviceMgr, s.configHasher, dnsCheckSubscriber, dnsServersProvider)

	// Create HTTP server
	s.httpServer = &http.Server{
		Addr:         bindAddr,
		Handler:      router,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// Create restartable runner for API server
	s.apiRunner = NewRestartableRunner(RunnerConfig{
		Name:           "API server",
		MaxRestarts:    0, // Unlimited restarts
		RestartBackoff: 2 * time.Second,
		MaxBackoff:     30 * time.Second,
	}, func(runCtx context.Context) error {
		log.Infof("API server listening on http://%s", bindAddr)
		err := s.httpServer.ListenAndServe()
		if err == http.ErrServerClosed {
			return nil // Clean shutdown
		}
		return err
	})

	// Start the runner
	if err := s.apiRunner.Start(ctx); err != nil {
		return err
	}

	return nil
}

// startDNSProxy initializes and starts the DNS proxy.
// Note: DNS redirect iptables rules are managed by NetworkManager via global components,
// not directly by this function. The global config is set when service starts.
func (s *ServiceCommand) startDNSProxy() error {
	proxyCfg := dnsproxy.ProxyConfigFromAppConfig(s.cfg)

	// Apply max cache domains from config
	proxyCfg.MaxCacheDomains = s.cfg.General.GetDNSCacheMaxDomains()

	// Create DNS proxy
	proxy, err := dnsproxy.NewDNSProxy(
		proxyCfg,
		s.deps.KeeneticClient(),
		s.deps.IPSetManager(),
		s.cfg,
	)
	if err != nil {
		return fmt.Errorf("failed to create DNS proxy: %w", err)
	}
	s.dnsProxy = proxy

	// Start the DNS proxy listener
	if err := s.dnsProxy.Start(); err != nil {
		s.dnsProxy = nil
		return fmt.Errorf("failed to start DNS proxy: %w", err)
	}

	// Note: DNS redirect iptables rules are applied by NetworkManager.ApplyPersistentConfig()
	// which is called by ServiceManager.run() after setting the global config

	log.Infof("DNS proxy started on port %d with upstream %v (cache: %d domains)", proxyCfg.ListenPort, proxyCfg.Upstreams, proxyCfg.MaxCacheDomains)
	return nil
}

// stopDNSProxy stops the DNS proxy.
// Note: DNS redirect iptables rules are removed by NetworkManager.UndoConfig()
// which is called by ServiceManager during shutdown.
func (s *ServiceCommand) stopDNSProxy() {
	if s.dnsProxy != nil {
		log.Infof("Stopping DNS proxy...")
		if err := s.dnsProxy.Stop(); err != nil {
			log.Errorf("Failed to stop DNS proxy: %v", err)
		}
		s.dnsProxy = nil
	}
}

// shutdown performs graceful shutdown of all components.
func (s *ServiceCommand) shutdown() error {
	log.Infof("Shutting down keen-pbr service...")

	// Stop the routing service
	log.Infof("Stopping routing service...")
	if err := s.serviceMgr.Stop(); err != nil {
		log.Errorf("Failed to stop routing service: %v", err)
	}

	// Stop DNS proxy
	s.stopDNSProxy()

	// Stop API server
	if s.httpServer != nil {
		log.Infof("Stopping API server...")
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		if err := s.httpServer.Shutdown(shutdownCtx); err != nil {
			log.Errorf("Error during API server shutdown: %v", err)
			s.httpServer.Close()
		}
	}

	// Stop API runner
	if s.apiRunner != nil {
		s.apiRunner.Stop()
	}

	log.Infof("Service stopped successfully")
	return nil
}

// Implement ServiceManager interface methods needed by API
var _ api.ServiceManager = (*ServiceManager)(nil)

// DownloadLists downloads all lists and reimports them to ipsets.
func (sm *ServiceManager) DownloadLists() error {
	sm.mu.RLock()
	cfg := sm.cfg
	deps := sm.deps
	sm.mu.RUnlock()

	if cfg == nil {
		// Load config if not already loaded
		var err error
		cfg, err = config.LoadConfig(sm.ctx.ConfigPath)
		if err != nil {
			return fmt.Errorf("failed to load config: %w", err)
		}
	}

	// Download all lists
	log.Infof("Downloading lists...")
	if err := lists.DownloadListsForced(cfg); err != nil {
		return fmt.Errorf("failed to download lists: %w", err)
	}

	// Re-import lists to ipsets if service is running
	if sm.IsRunning() {
		log.Infof("Re-importing lists to ipsets...")
		if err := lists.ImportListsToIPSets(cfg, deps.ListManager()); err != nil {
			return fmt.Errorf("failed to re-import lists: %w", err)
		}

		// Notify listeners
		sm.mu.RLock()
		callback := sm.onListsUpdated
		sm.mu.RUnlock()
		if callback != nil {
			callback()
		}
	}

	return nil
}
