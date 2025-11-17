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

// RestartServicesAfterConfigChange performs the complete service restart sequence:
// 1. Stop keen-pbr service
// 2. Flush all ipsets
// 3. Start keen-pbr service
// 4. Restart dnsmasq
func RestartServicesAfterConfigChange(cfg *config.Config) error {
	log.Infof("[API] Restarting services after configuration change")

	// 1. Stop keen-pbr service
	log.Debugf("[API] Stopping keen-pbr service")
	if err := exec.Command("/opt/etc/init.d/S80keen-pbr", "stop").Run(); err != nil {
		log.Warnf("[API] Failed to stop keen-pbr service: %v", err)
		// Continue anyway - service might not be running
	}

	// 2. Flush all ipsets
	log.Debugf("[API] Flushing all ipsets")
	for _, ipset := range cfg.IPSets {
		log.Debugf("[API] Flushing ipset: %s", ipset.IPSetName)
		if err := exec.Command("ipset", "flush", ipset.IPSetName).Run(); err != nil {
			log.Warnf("[API] Failed to flush ipset %s: %v", ipset.IPSetName, err)
			// Continue anyway - ipset might not exist yet
		}
	}

	// 3. Start keen-pbr service
	log.Debugf("[API] Starting keen-pbr service")
	if err := exec.Command("/opt/etc/init.d/S80keen-pbr", "start").Run(); err != nil {
		return fmt.Errorf("failed to start keen-pbr service: %w", err)
	}

	// 4. Restart dnsmasq to re-read domain lists
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
func ModifyConfig(configPath string, modifyFn func(*config.Config) error) error {
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
	if err := RestartServicesAfterConfigChange(cfg); err != nil {
		return fmt.Errorf("failed to restart services: %w", err)
	}

	return nil
}
