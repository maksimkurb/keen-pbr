package api

import (
	"fmt"
	"os/exec"
	"sync"

	"github.com/maksimkurb/keen-pbr/lib/config"
	"github.com/maksimkurb/keen-pbr/lib/log"
)

// configMutex protects concurrent config file access
var configMutex sync.RWMutex

// RestartServicesAfterConfigChange restarts the integrated routing service after configuration changes.
// This uses the routing service's internal restart mechanism instead of calling init.d scripts.
// Sequence:
// 1. Restart integrated routing service (stops service, flushes ipsets, starts service)
// 2. Restart dnsmasq to reload domain lists
func RestartServicesAfterConfigChange(routingService *RoutingService) error {
	log.Infof("[API] Restarting services after configuration change")

	// 1. Restart the integrated routing service
	if err := routingService.Restart(); err != nil {
		return fmt.Errorf("failed to restart routing service: %w", err)
	}

	// 2. Restart dnsmasq to re-read domain lists
	log.Debugf("[API] Restarting dnsmasq service")
	if err := exec.Command("/opt/etc/init.d/S56dnsmasq", "restart").Run(); err != nil {
		log.Warnf("[API] Failed to restart dnsmasq: %v", err)
		// Continue anyway - dnsmasq might not be installed in dev environment
	}

	log.Infof("[API] Services restarted successfully")
	return nil
}

// LoadConfig loads the configuration with read lock
func LoadConfig(configPath string) (*config.Config, error) {
	configMutex.RLock()
	defer configMutex.RUnlock()
	return config.LoadConfig(configPath)
}

// ModifyConfig executes a function that modifies the config, validates, writes, and restarts services
func ModifyConfig(configPath string, routingService *RoutingService, modifyFn func(*config.Config) error) error {
	configMutex.Lock()
	defer configMutex.Unlock()

	// Load config
	cfg, err := config.LoadConfig(configPath)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	// Modify config
	if err := modifyFn(cfg); err != nil {
		return err
	}

	// Write config
	if err := cfg.WriteConfig(); err != nil {
		return fmt.Errorf("failed to write config: %w", err)
	}

	// Restart services
	if err := RestartServicesAfterConfigChange(routingService); err != nil {
		return fmt.Errorf("failed to restart services: %w", err)
	}

	return nil
}
