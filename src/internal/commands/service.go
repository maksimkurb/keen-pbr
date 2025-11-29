package commands

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/api"
	"github.com/maksimkurb/keen-pbr/src/internal/components"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// SIGUSR1 and SIGUSR2 debounce delay
const debounceDelay = 1000 * time.Millisecond

func CreateServiceCommand() *ServiceCommand {
	sc := &ServiceCommand{
		fs: flag.NewFlagSet("service", flag.ExitOnError),
	}

	sc.fs.StringVar(&sc.APIBindAddress, "api", "", "Enable REST API and web UI on the specified bind address (e.g., 0.0.0.0:8080)")

	return sc
}

type ServiceCommand struct {
	fs             *flag.FlagSet
	cfg            *config.Config
	ctx            *AppContext
	APIBindAddress string // Command-line flag for API server bind address

	// Dependencies
	deps         *domain.AppDependencies
	networkMgr   domain.NetworkManager
	configHasher *config.ConfigHasher

	// Service manager for controlling the routing service
	serviceMgr *ServiceManager

	// Components
	networkingSvc *components.NetworkingService
	apiServer     *components.APIServer
}

func (s *ServiceCommand) Name() string {
	return s.fs.Name()
}

// StartService starts the routing service and DNS proxy
// This method can be called from both CLI and API
func (s *ServiceCommand) StartService() error {
	if s.networkingSvc == nil {
		return fmt.Errorf("networking service not initialized")
	}

	// Start the networking service (routing + DNS proxy)
	if err := s.networkingSvc.Start(); err != nil {
		return fmt.Errorf("failed to start networking service: %w", err)
	}

	return nil
}

// StopService stops the routing service and DNS proxy
func (s *ServiceCommand) StopService() error {
	if s.networkingSvc == nil {
		return fmt.Errorf("networking service not initialized")
	}

	// Stop the networking service
	if err := s.networkingSvc.Stop(); err != nil {
		return fmt.Errorf("failed to stop networking service: %w", err)
	}

	return nil
}

func (s *ServiceCommand) Init(args []string, ctx *AppContext) error {
	s.ctx = ctx
	if err := s.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		s.cfg = cfg
	}

	// Create dependencies
	s.deps = domain.NewDefaultDependencies()
	s.networkMgr = s.deps.NetworkManager()

	// Create ConfigHasher DI component
	s.configHasher = config.NewConfigHasher(ctx.ConfigPath)

	// Create service manager
	serviceMgr, err := NewServiceManager(ctx, s.configHasher)
	if err != nil {
		return fmt.Errorf("failed to create service manager: %w", err)
	}
	s.serviceMgr = serviceMgr

	// Create networking service component
	s.networkingSvc = components.NewNetworkingService(ctx.ConfigPath, s.serviceMgr, s.deps)

	return nil
}

// debouncedRefresh creates a debounced operation that waits for any ongoing execution
func debouncedRefresh(
	timer **time.Timer,
	mu *sync.Mutex,
	delay time.Duration,
	name string,
	operation func() error,
) {
	// Cancel existing timer if any
	if *timer != nil {
		(*timer).Stop()
	}

	// Create new timer that will execute after delay
	*timer = time.AfterFunc(delay, func() {
		// Wait for any ongoing execution to complete
		mu.Lock()
		defer mu.Unlock()

		log.Infof("Executing %s", name)
		if err := operation(); err != nil {
			log.Errorf("Failed to execute %s: %v", name, err)
		} else {
			log.Debugf("%s executed successfully", name)
		}
	})
}

func (s *ServiceCommand) Run() error {
	log.Infof("Starting keen-pbr...")

	// Setup signal handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP, syscall.SIGUSR1, syscall.SIGUSR2)

	// Try to start the service
	serviceStartErr := s.StartService()
	if serviceStartErr != nil {
		log.Errorf("Failed to start service: %v", serviceStartErr)

		// If API is disabled and service failed to start, exit immediately
		if s.APIBindAddress == "" {
			return fmt.Errorf("service failed to start and API is disabled, exiting")
		}

		log.Warnf("Service will remain stopped. Fix the configuration and restart via API or SIGHUP.")
	}

	// Start HTTP API server if --api flag is provided
	// API server starts regardless of service state to allow config fixes
	if s.APIBindAddress != "" {
		if err := s.startAPIServer(); err != nil {
			log.Errorf("Failed to start API server: %v", err)
			log.Warnf("Web UI will not be available")
		}
	} else {
		log.Infof("REST API and web UI is disabled (use --api flag to enable)")
	}

	if serviceStartErr == nil {
		log.Infof("keen-pbr started successfully")
	} else {
		log.Infof("keen-pbr started with API only (service failed)")
	}
	log.Infof("Send SIGHUP to reload configuration, SIGUSR1 to refresh routing, SIGUSR2 to refresh firewall")

	// Debounce timers and mutexes for SIGUSR1 and SIGUSR2
	var routingTimer *time.Timer
	var firewallTimer *time.Timer
	var routingMu sync.Mutex
	var firewallMu sync.Mutex

	// Run signal handling loop
	for sig := range sigChan {
		switch sig {
		case syscall.SIGHUP:
			log.Infof("Received SIGHUP signal, reloading configuration...")

			// Stop service if running
			if s.networkingSvc != nil && s.networkingSvc.IsRunning() {
				if err := s.StopService(); err != nil {
					log.Errorf("Failed to stop service: %v", err)
				}
			}

			// Try to start service with new config
			if err := s.StartService(); err != nil {
				log.Errorf("Failed to start service after reload: %v", err)
			} else {
				log.Infof("Configuration reloaded successfully")
			}

		case syscall.SIGUSR1:
			debouncedRefresh(&routingTimer, &routingMu, debounceDelay, "SIGUSR1 routing refresh", func() error {
				if !s.networkingSvc.IsRunning() {
					return nil
				}
				return s.serviceMgr.RefreshRouting()
			})

		case syscall.SIGUSR2:
			debouncedRefresh(&firewallTimer, &firewallMu, debounceDelay, "SIGUSR2 firewall refresh", func() error {
				if !s.networkingSvc.IsRunning() {
					return nil
				}
				return s.serviceMgr.RefreshFirewall()
			})

		case syscall.SIGINT, syscall.SIGTERM:
			log.Infof("Received signal %v, shutting down...", sig)
			// Stop any pending timers
			if routingTimer != nil {
				routingTimer.Stop()
			}
			if firewallTimer != nil {
				firewallTimer.Stop()
			}
			return s.shutdown()
		}
	}
	return nil
}

// startAPIServer starts the HTTP API server using the APIServer component.
func (s *ServiceCommand) startAPIServer() error {
	// Create API server component
	s.apiServer = components.NewAPIServer(
		s.APIBindAddress,
		s.ctx.ConfigPath,
		s.deps,
		s.serviceMgr,
		s.configHasher,
		s.networkingSvc,
	)

	// Start the API server
	if err := s.apiServer.Start(); err != nil {
		return fmt.Errorf("failed to start API server: %w", err)
	}

	return nil
}

// shutdown performs graceful shutdown of all components.
func (s *ServiceCommand) shutdown() error {
	log.Infof("Shutting down keen-pbr service...")

	var errs []error

	// Stop networking service
	if s.networkingSvc != nil {
		if err := s.networkingSvc.Stop(); err != nil {
			log.Errorf("Failed to stop networking service: %v", err)
			errs = append(errs, fmt.Errorf("networking service: %w", err))
		}
	}

	// Stop API server
	if s.apiServer != nil {
		if err := s.apiServer.Stop(); err != nil {
			log.Errorf("Error stopping API server: %v", err)
			errs = append(errs, fmt.Errorf("API server: %w", err))
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("shutdown errors: %v", errs)
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
	if err := lists.DownloadListsIfUpdated(cfg); err != nil {
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
