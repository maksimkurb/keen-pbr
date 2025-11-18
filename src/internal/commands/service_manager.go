package commands

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// ServiceManager manages the lifecycle of the keen-pbr service
// allowing it to be started, stopped, and restarted as a goroutine
type ServiceManager struct {
	mu              sync.RWMutex
	ctx             *AppContext
	cfg             *config.Config
	monitorInterval int
	running         bool
	cancel          context.CancelFunc
	networkMgr      domain.NetworkManager
	deps            *domain.AppDependencies
	done            chan error
}

// NewServiceManager creates a new service manager
func NewServiceManager(ctx *AppContext, monitorInterval int) (*ServiceManager, error) {
	cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath)
	if err != nil {
		return nil, fmt.Errorf("failed to load configuration: %w", err)
	}

	if err := networking.ValidateInterfacesArePresent(cfg, ctx.Interfaces); err != nil {
		return nil, fmt.Errorf("failed to validate interfaces: %w", err)
	}

	deps := domain.NewDefaultDependencies()

	return &ServiceManager{
		ctx:             ctx,
		cfg:             cfg,
		monitorInterval: monitorInterval,
		running:         false,
		deps:            deps,
		networkMgr:      deps.NetworkManager(),
	}, nil
}

// IsRunning returns true if the service is currently running
func (sm *ServiceManager) IsRunning() bool {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	return sm.running
}

// Start starts the service in a goroutine
func (sm *ServiceManager) Start() error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if sm.running {
		return fmt.Errorf("service is already running")
	}

	// Create context for service control
	ctx, cancel := context.WithCancel(context.Background())
	sm.cancel = cancel
	sm.done = make(chan error, 1)

	// Start service in goroutine
	go sm.run(ctx)

	sm.running = true
	log.Infof("Service started successfully")
	return nil
}

// Stop stops the running service gracefully
func (sm *ServiceManager) Stop() error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if !sm.running {
		return fmt.Errorf("service is not running")
	}

	log.Infof("Stopping service...")

	// Cancel the context to signal shutdown
	if sm.cancel != nil {
		sm.cancel()
	}

	// Wait for service to finish with timeout
	select {
	case err := <-sm.done:
		if err != nil {
			log.Errorf("Service stopped with error: %v", err)
			sm.running = false
			return err
		}
	case <-time.After(30 * time.Second):
		sm.running = false
		return fmt.Errorf("timeout waiting for service to stop")
	}

	sm.running = false
	log.Infof("Service stopped successfully")
	return nil
}

// Restart restarts the service
func (sm *ServiceManager) Restart() error {
	if err := sm.Stop(); err != nil {
		// If service wasn't running, that's okay, we'll start it anyway
		if sm.IsRunning() {
			return fmt.Errorf("failed to stop service: %w", err)
		}
	}

	// Small delay to ensure cleanup is complete
	time.Sleep(500 * time.Millisecond)

	return sm.Start()
}

// run is the main service loop (runs in a goroutine)
func (sm *ServiceManager) run(ctx context.Context) {
	defer close(sm.done)

	log.Infof("Starting keen-pbr service...")

	// Initial setup: create ipsets and fill them
	log.Infof("Importing lists to ipsets...")
	if err := lists.ImportListsToIPSets(sm.cfg, sm.deps.ListManager()); err != nil {
		sm.done <- fmt.Errorf("failed to import lists: %w", err)
		return
	}

	// Apply persistent network configuration (iptables rules and ip rules)
	log.Infof("Applying persistent network configuration (iptables rules and ip rules)...")
	if err := sm.networkMgr.ApplyPersistentConfig(sm.cfg.IPSets); err != nil {
		sm.done <- fmt.Errorf("failed to apply persistent network configuration: %w", err)
		return
	}

	// Apply initial routing (ip routes)
	log.Infof("Applying initial routing configuration...")
	if err := sm.networkMgr.ApplyRoutingConfig(sm.cfg.IPSets); err != nil {
		sm.done <- fmt.Errorf("failed to apply routing configuration: %w", err)
		return
	}

	log.Infof("Service started successfully. Monitoring interface changes every %d seconds...", sm.monitorInterval)

	// Start monitoring loop
	ticker := time.NewTicker(time.Duration(sm.monitorInterval) * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			log.Infof("Service context cancelled, shutting down...")
			sm.done <- sm.shutdown()
			return

		case <-ticker.C:
			// Update interface list
			var err error
			if sm.ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
				log.Errorf("Failed to get interfaces list: %v", err)
				continue
			}

			// Update routing configuration (only if interfaces changed)
			log.Debugf("Checking interface states...")
			if _, err := sm.networkMgr.UpdateRoutingIfChanged(sm.cfg.IPSets); err != nil {
				log.Errorf("Failed to update routing configuration: %v", err)
			}
		}
	}
}

// shutdown performs cleanup when the service stops
func (sm *ServiceManager) shutdown() error {
	log.Infof("Shutting down keen-pbr service...")

	// Remove all network configuration
	if err := sm.networkMgr.UndoConfig(sm.cfg.IPSets); err != nil {
		log.Errorf("Failed to undo routing configuration: %v", err)
		return err
	}

	log.Infof("Service shutdown complete")
	return nil
}
