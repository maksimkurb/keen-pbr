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
	configHasher    *config.ConfigHasher // DI component for hash tracking
	onListsUpdated  func()               // Callback when lists are updated (e.g., for DNS proxy reload)
}

// NewServiceManager creates a new service manager
// Note: This does not validate runtime requirements (like interface presence)
// Those validations happen in Start() to allow the API server to start even with invalid config
func NewServiceManager(ctx *AppContext, monitorInterval int, configHasher *config.ConfigHasher) (*ServiceManager, error) {
	deps := domain.NewDefaultDependencies()

	return &ServiceManager{
		ctx:             ctx,
		cfg:             nil, // Will be loaded in Start()
		monitorInterval: monitorInterval,
		running:         false,
		deps:            deps,
		networkMgr:      deps.NetworkManager(),
		configHasher:    configHasher,
	}, nil
}

// IsRunning returns true if the service is currently running
func (sm *ServiceManager) IsRunning() bool {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	return sm.running
}

// GetAppliedConfigHash returns the hash of applied configuration
// Delegates to ConfigHasher DI component
func (sm *ServiceManager) GetAppliedConfigHash() string {
	return sm.configHasher.GetKeenPbrActiveConfigHash()
}

// SetOnListsUpdated sets a callback that will be invoked when lists are updated.
// This is used to notify the DNS proxy to reload its domain lists.
func (sm *ServiceManager) SetOnListsUpdated(callback func()) {
	sm.mu.Lock()
	defer sm.mu.Unlock()
	sm.onListsUpdated = callback
}

// Start starts the service in a goroutine
func (sm *ServiceManager) Start() error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if sm.running {
		return fmt.Errorf("service is already running")
	}

	// Load and validate configuration before starting
	cfg, err := loadAndValidateConfigOrFail(sm.ctx.ConfigPath)
	if err != nil {
		return fmt.Errorf("configuration validation failed: %w", err)
	}

	// Validate that required interfaces exist
	if err := networking.ValidateInterfacesArePresent(cfg, sm.ctx.Interfaces); err != nil {
		networking.PrintMissingInterfacesHelp()
		return fmt.Errorf("interface validation failed: %w", err)
	}

	// Store validated config
	sm.cfg = cfg

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
	// Phase 1: Cancel the context (with lock held)
	sm.mu.Lock()
	if !sm.running {
		sm.mu.Unlock()
		return fmt.Errorf("service is not running")
	}

	log.Infof("Stopping service...")

	// Cancel the context to signal shutdown
	cancel := sm.cancel
	done := sm.done
	sm.mu.Unlock()

	// Phase 2: Wait for service to finish (WITHOUT holding lock)
	// This allows API handlers to check service status without deadlocking
	if cancel != nil {
		cancel()
	}

	var stopErr error
	select {
	case err := <-done:
		if err != nil {
			log.Errorf("Service stopped with error: %v", err)
			stopErr = err
		}
	case <-time.After(30 * time.Second):
		stopErr = fmt.Errorf("timeout waiting for service to stop")
	}

	// Phase 3: Update running state (with lock held)
	sm.mu.Lock()
	sm.running = false
	sm.mu.Unlock()

	if stopErr != nil {
		return stopErr
	}

	log.Infof("Service stopped successfully")
	return nil
}

// Reload reloads the configuration and reapplies network settings
// This is typically triggered by SIGHUP signal
func (sm *ServiceManager) Reload() error {
	sm.mu.RLock()
	if !sm.running {
		sm.mu.RUnlock()
		return fmt.Errorf("service is not running")
	}
	cfg := sm.cfg
	ctx := sm.ctx
	networkMgr := sm.networkMgr
	sm.mu.RUnlock()

	log.Infof("Reloading configuration (triggered by SIGHUP)...")

	// Update interface list
	var err error
	if ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
		return fmt.Errorf("failed to get interfaces list: %v", err)
	}

	// Update global config for service-level components (DNS redirect, etc.)
	globalCfg := networking.GlobalConfigFromAppConfig(cfg)
	networkMgr.SetGlobalConfig(globalCfg)

	// Reapply persistent network configuration (iptables rules, ip rules, and global components)
	log.Infof("Reapplying persistent network configuration...")
	if err := networkMgr.ApplyPersistentConfig(cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	// Force update routing configuration (ip routes)
	// SIGHUP forces a full refresh, not just changed interfaces
	log.Infof("Forcing routing configuration update...")
	if err := networkMgr.ApplyRoutingConfig(cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply routing configuration: %v", err)
	}

	log.Infof("Configuration reloaded successfully")
	return nil
}

// RefreshRoutingAndFirewall refreshes the routing configuration and firewall rules.
// This is typically triggered by SIGUSR1 signal.
// It checks interfaces, updates routing if changed, and refreshes persistent config (iptables/rules).
func (sm *ServiceManager) RefreshRoutingAndFirewall() error {
	sm.mu.RLock()
	if !sm.running {
		sm.mu.RUnlock()
		return fmt.Errorf("service is not running")
	}
	cfg := sm.cfg
	ctx := sm.ctx
	networkMgr := sm.networkMgr
	sm.mu.RUnlock()

	log.Debugf("Refreshing routing and firewall (triggered by SIGUSR1)...")

	// Update interface list
	var err error
	if ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
		return fmt.Errorf("failed to get interfaces list: %v", err)
	}

	// Re-apply global components (DNS redirect rules need updating when IPs change)
	globalCfg := networking.GlobalConfigFromAppConfig(cfg)
	networkMgr.SetGlobalConfig(globalCfg)

	// Re-apply persistent config to update DNS redirect rules and other persistent rules
	log.Debugf("Refreshing persistent network configuration (iptables, ip rules)...")
	if err := networkMgr.ApplyPersistentConfig(cfg.IPSets); err != nil {
		return fmt.Errorf("failed to refresh persistent network configuration: %v", err)
	}

	// Update routing configuration only if interfaces changed
	log.Debugf("Checking for interface changes and updating routing if needed...")
	changedCount, err := networkMgr.UpdateRoutingIfChanged(cfg.IPSets)
	if err != nil {
		return fmt.Errorf("failed to update routing configuration: %v", err)
	}

	if changedCount > 0 {
		log.Debugf("Routing updated: %d interface(s) changed", changedCount)
	} else {
		log.Debugf("No interface changes detected, routing unchanged")
	}

	return nil
}

// RefreshFirewall refreshes only the firewall rules (iptables) and ip rules.
// This is typically triggered by SIGUSR2 signal.
func (sm *ServiceManager) RefreshFirewall() error {
	sm.mu.RLock()
	if !sm.running {
		sm.mu.RUnlock()
		return fmt.Errorf("service is not running")
	}
	cfg := sm.cfg
	networkMgr := sm.networkMgr
	sm.mu.RUnlock()

	log.Debugf("Refreshing firewall rules (triggered by SIGUSR2)...")

	// Re-apply global components
	globalCfg := networking.GlobalConfigFromAppConfig(cfg)
	networkMgr.SetGlobalConfig(globalCfg)

	// Re-apply persistent config
	log.Debugf("Reapplying persistent network configuration...")
	if err := networkMgr.ApplyPersistentConfig(cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	log.Infof("Firewall rules refreshed successfully")
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

	// Start() will handle config loading and validation
	return sm.Start()
}

// run is the main service loop (runs in a goroutine)
func (sm *ServiceManager) run(ctx context.Context) {
	defer close(sm.done)

	log.Infof("Starting keen-pbr service...")

	// Capture config snapshot at startup - this is immutable for this service instance
	// This ensures shutdown uses the same config that was used for startup,
	// properly cleaning up all ipsets even if config changes on disk
	sm.mu.RLock()
	startupCfg := sm.cfg
	sm.mu.RUnlock()

	// Calculate and store active config hash at startup
	configHash, err := sm.configHasher.UpdateCurrentConfigHash()
	if err != nil {
		log.Warnf("Failed to calculate config hash: %v", err)
		configHash = "unknown"
	}
	sm.configHasher.SetKeenPbrActiveConfigHash(configHash)
	log.Infof("Service started with config hash: %s", configHash)

	// Download missing lists (only downloads if files don't exist)
	log.Infof("Checking and downloading missing lists...")
	if err := lists.DownloadLists(startupCfg); err != nil {
		log.Warnf("Some lists failed to download: %v", err)
	}

	// Initial setup: create ipsets and fill them
	log.Infof("Importing lists to ipsets...")
	if err := lists.ImportListsToIPSets(startupCfg, sm.deps.ListManager()); err != nil {
		sm.done <- fmt.Errorf("failed to import lists: %w", err)
		return
	}

	// Set global config for service-level components (DNS redirect, etc.)
	// This must be done before ApplyPersistentConfig
	globalCfg := networking.GlobalConfigFromAppConfig(startupCfg)
	sm.networkMgr.SetGlobalConfig(globalCfg)

	// Apply persistent network configuration (iptables rules, ip rules, and global components)
	log.Infof("Applying persistent network configuration (iptables rules, ip rules, and global components)...")
	if err := sm.networkMgr.ApplyPersistentConfig(startupCfg.IPSets); err != nil {
		sm.done <- fmt.Errorf("failed to apply persistent network configuration: %w", err)
		return
	}

	// Apply initial routing (ip routes)
	log.Infof("Applying initial routing configuration...")
	if err := sm.networkMgr.ApplyRoutingConfig(startupCfg.IPSets); err != nil {
		sm.done <- fmt.Errorf("failed to apply routing configuration: %w", err)
		return
	}

	log.Infof("Service started successfully.")

	// Start interface monitoring if enabled
	var interfaceTicker *time.Ticker
	if startupCfg.General.IsInterfaceMonitoringEnabled() {
		log.Infof("Interface monitoring enabled. Will check interface changes every %d seconds", sm.monitorInterval)
		interfaceTicker = time.NewTicker(time.Duration(sm.monitorInterval) * time.Second)
		defer interfaceTicker.Stop()
	} else {
		log.Infof("Interface monitoring disabled")
	}

	// Start auto-update timer if enabled
	var updateTicker *time.Ticker
	if startupCfg.General.IsAutoUpdateEnabled() {
		interval := startupCfg.General.GetUpdateIntervalHours()
		log.Infof("List auto-update enabled. Will check for updates every %d hour(s)", interval)
		updateTicker = time.NewTicker(time.Duration(interval) * time.Hour)
		defer updateTicker.Stop()
	} else {
		log.Infof("List auto-update disabled")
	}

	for {
		select {
		case <-ctx.Done():
			log.Infof("Service context cancelled, shutting down...")
			sm.done <- sm.shutdownWithConfig(startupCfg)
			return

		case <-func() <-chan time.Time {
			if interfaceTicker != nil {
				return interfaceTicker.C
			}
			// Return a channel that never sends if interface monitoring is disabled
			return make(<-chan time.Time)
		}():
			// Update interface list
			var err error
			if sm.ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
				log.Errorf("Failed to get interfaces list: %v", err)
				continue
			}

			// Update routing configuration (only if interfaces changed)
			log.Debugf("Checking interface states...")
			if _, err := sm.networkMgr.UpdateRoutingIfChanged(startupCfg.IPSets); err != nil {
				log.Errorf("Failed to update routing configuration: %v", err)
			}

		case <-func() <-chan time.Time {
			if updateTicker != nil {
				return updateTicker.C
			}
			// Return a channel that never sends if auto-update is disabled
			return make(<-chan time.Time)
		}():
			// Auto-update lists
			log.Infof("Running scheduled list update...")
			if err := sm.updateLists(startupCfg); err != nil {
				log.Errorf("Failed to update lists: %v", err)
			}
		}
	}
}

// updateLists downloads and re-imports lists
func (sm *ServiceManager) updateLists(cfg *config.Config) error {
	// Download all lists (forced update)
	log.Infof("Downloading updated lists...")
	if err := lists.DownloadListsForced(cfg); err != nil {
		return fmt.Errorf("failed to download lists: %w", err)
	}

	// Re-import lists to ipsets
	log.Infof("Re-importing lists to ipsets...")
	if err := lists.ImportListsToIPSets(cfg, sm.deps.ListManager()); err != nil {
		return fmt.Errorf("failed to re-import lists: %w", err)
	}

	// Notify listeners (e.g., DNS proxy) that lists have been updated
	sm.mu.RLock()
	callback := sm.onListsUpdated
	sm.mu.RUnlock()
	if callback != nil {
		callback()
	}

	log.Infof("List update completed successfully")
	return nil
}

// shutdownWithConfig performs cleanup when the service stops
// Uses the startup config to ensure all ipsets created at startup are properly removed
func (sm *ServiceManager) shutdownWithConfig(startupCfg *config.Config) error {
	log.Infof("Shutting down keen-pbr service...")

	// Remove all network configuration using the startup config
	// This ensures we clean up exactly what was created, even if config changed on disk
	if err := sm.networkMgr.UndoConfig(startupCfg.IPSets); err != nil {
		log.Errorf("Failed to undo routing configuration: %v", err)
		return err
	}

	log.Infof("Service shutdown complete")
	return nil
}
