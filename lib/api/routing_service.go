package api

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/lib/config"
	"github.com/maksimkurb/keen-pbr/lib/lists"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/networking"
)

// RoutingService manages the routing service lifecycle
type RoutingService struct {
	configPath string
	interfaces []networking.Interface

	mu         sync.RWMutex
	ctx        context.Context
	cancel     context.CancelFunc
	running    bool
	wg         sync.WaitGroup
}

// NewRoutingService creates a new routing service
func NewRoutingService(configPath string, interfaces []networking.Interface) *RoutingService {
	return &RoutingService{
		configPath: configPath,
		interfaces: interfaces,
		running:    false,
	}
}

// Start starts the routing service
func (rs *RoutingService) Start() error {
	rs.mu.Lock()
	defer rs.mu.Unlock()

	if rs.running {
		return fmt.Errorf("routing service already running")
	}

	// Create context for this service run
	rs.ctx, rs.cancel = context.WithCancel(context.Background())

	// Load and validate config
	cfg, err := config.LoadConfig(rs.configPath)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	// Validate interfaces
	if err := networking.ValidateInterfacesArePresent(cfg, rs.interfaces); err != nil {
		log.Warnf("[Routing] Interface validation failed (will retry periodically): %v", err)
	}

	// Apply initial configuration
	log.Infof("[Routing] Starting routing service")
	if err := rs.applyConfiguration(cfg); err != nil {
		log.Warnf("[Routing] Failed to fully apply initial configuration (will retry): %v", err)
	}

	rs.running = true
	log.Infof("[Routing] Routing service started successfully")

	// Start monitoring goroutine (checks for interface changes, reapplies rules if needed)
	rs.wg.Add(1)
	go rs.monitorLoop()

	return nil
}

// Stop stops the routing service
func (rs *RoutingService) Stop() error {
	rs.mu.Lock()
	defer rs.mu.Unlock()

	if !rs.running {
		return fmt.Errorf("routing service not running")
	}

	log.Infof("[Routing] Stopping routing service")

	// Cancel context to stop monitoring loop
	rs.cancel()

	rs.running = false

	// Wait for monitoring goroutine to finish
	rs.wg.Wait()

	log.Infof("[Routing] Routing service stopped")

	return nil
}

// Restart restarts the routing service
func (rs *RoutingService) Restart() error {
	log.Infof("[Routing] Restarting routing service")

	// Stop if running
	if rs.IsRunning() {
		if err := rs.Stop(); err != nil {
			return fmt.Errorf("failed to stop service: %w", err)
		}
	}

	// Flush ipsets
	cfg, err := config.LoadConfig(rs.configPath)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	log.Infof("[Routing] Flushing ipsets")
	if err := lists.FlushAllIPSets(cfg); err != nil {
		log.Warnf("[Routing] Failed to flush some ipsets: %v", err)
	}

	// Start again
	if err := rs.Start(); err != nil {
		return fmt.Errorf("failed to start service: %w", err)
	}

	log.Infof("[Routing] Routing service restarted successfully")
	return nil
}

// IsRunning returns whether the service is running
func (rs *RoutingService) IsRunning() bool {
	rs.mu.RLock()
	defer rs.mu.RUnlock()
	return rs.running
}

// GetStatus returns the service status
func (rs *RoutingService) GetStatus() string {
	if rs.IsRunning() {
		return "running"
	}
	return "stopped"
}

// applyConfiguration applies the routing configuration (import lists, apply routes)
func (rs *RoutingService) applyConfiguration(cfg *config.Config) error {
	log.Infof("[Routing] Importing lists to ipsets")
	if err := lists.ImportListsToIPSets(cfg); err != nil {
		return fmt.Errorf("failed to import lists: %w", err)
	}

	log.Infof("[Routing] Applying network configuration")
	if appliedAtLeastOnce, err := networking.ApplyNetworkConfiguration(cfg, nil); err != nil {
		return fmt.Errorf("failed to apply routing: %w", err)
	} else if !appliedAtLeastOnce {
		log.Warnf("[Routing] No routing configuration applied (all interfaces down?)")
	}

	return nil
}

// monitorLoop monitors for interface changes and reapplies configuration as needed
func (rs *RoutingService) monitorLoop() {
	defer rs.wg.Done()

	// Check every 30 seconds if routing needs to be reapplied
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-rs.ctx.Done():
			// Service stopped
			return
		case <-ticker.C:
			// Reload config and reapply if needed
			cfg, err := config.LoadConfig(rs.configPath)
			if err != nil {
				log.Warnf("[Routing] Failed to reload config: %v", err)
				continue
			}

			// Reapply network configuration (will check if interfaces are up)
			if _, err := networking.ApplyNetworkConfiguration(cfg, nil); err != nil {
				log.Warnf("[Routing] Failed to reapply routing: %v", err)
			}
		}
	}
}
