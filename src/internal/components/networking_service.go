package components

import (
	"fmt"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// NetworkingService manages the routing service and DNS proxy
type NetworkingService struct {
	configPath string
	serviceMgr ServiceManager
	deps       *domain.AppDependencies
	dnsProxy   *dnsproxy.DNSProxy
	cfg        *config.Config
	running    bool
	mu         sync.Mutex
}

// NewNetworkingService creates a new networking service component
func NewNetworkingService(configPath string, serviceMgr ServiceManager, deps *domain.AppDependencies) *NetworkingService {
	return &NetworkingService{
		configPath: configPath,
		serviceMgr: serviceMgr,
		deps:       deps,
	}
}

// Start starts the routing service and DNS proxy
func (n *NetworkingService) Start() error {
	n.mu.Lock()
	defer n.mu.Unlock()

	if n.running {
		return fmt.Errorf("networking service is already running")
	}

	// Load config
	cfg, err := config.LoadConfig(n.configPath)
	if err != nil {
		return fmt.Errorf("failed to load configuration: %w", err)
	}
	n.cfg = cfg

	// Start the routing service
	log.Infof("Starting routing service...")
	if err := n.serviceMgr.Start(); err != nil {
		return fmt.Errorf("failed to start routing service: %w", err)
	}

	// Start DNS proxy if enabled
	if n.cfg.General.DNSServer != nil && n.cfg.General.DNSServer.Enable {
		if err := n.startDNSProxy(); err != nil {
			log.Errorf("Failed to start DNS proxy: %v", err)
			log.Warnf("Domain-based routing via DNS proxy will not be available")
		} else {
			// Register callback to reload DNS proxy lists when lists are updated
			n.serviceMgr.SetOnListsUpdated(func() {
				if n.dnsProxy != nil {
					n.dnsProxy.ReloadLists()
				}
			})
		}
	}

	n.running = true
	log.Infof("Networking service started successfully")
	return nil
}

// Stop stops the routing service and DNS proxy
func (n *NetworkingService) Stop() error {
	n.mu.Lock()
	defer n.mu.Unlock()

	if !n.running {
		return fmt.Errorf("networking service is not running")
	}

	log.Infof("Stopping networking service...")

	// Stop DNS proxy
	if n.dnsProxy != nil {
		if err := n.dnsProxy.Stop(); err != nil {
			log.Errorf("Error stopping DNS proxy: %v", err)
		}
		n.dnsProxy = nil
	}

	// Stop service manager
	if err := n.serviceMgr.Stop(); err != nil {
		log.Errorf("Error stopping service manager: %v", err)
	}

	n.running = false
	log.Infof("Networking service stopped")
	return nil
}

// IsRunning returns whether the service is running
func (n *NetworkingService) IsRunning() bool {
	n.mu.Lock()
	defer n.mu.Unlock()
	return n.running
}

// GetDNSProxy returns the DNS proxy instance (for API server)
func (n *NetworkingService) GetDNSProxy() interface{} {
	n.mu.Lock()
	defer n.mu.Unlock()
	return n.dnsProxy
}

// GetServiceManager returns the service manager (for signal handling)
func (n *NetworkingService) GetServiceManager() ServiceManager {
	return n.serviceMgr
}

// startDNSProxy starts the DNS proxy
func (n *NetworkingService) startDNSProxy() error {
	proxyCfg := dnsproxy.ProxyConfigFromAppConfig(n.cfg)
	proxyCfg.MaxCacheDomains = n.cfg.General.DNSServer.CacheMaxDomains

	proxy, err := dnsproxy.NewDNSProxy(
		proxyCfg,
		n.deps.KeeneticClient(),
		n.deps.IPSetManager(),
		n.cfg,
	)
	if err != nil {
		return fmt.Errorf("failed to create DNS proxy: %w", err)
	}

	if err := proxy.Start(); err != nil {
		return fmt.Errorf("failed to start DNS proxy: %w", err)
	}

	n.dnsProxy = proxy
	return nil
}
