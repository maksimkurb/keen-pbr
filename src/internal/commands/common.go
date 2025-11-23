package commands

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

type Runner interface {
	Init(args []string, globalArgs *AppContext) error
	Run() error
	Name() string
}

type AppContext struct {
	ConfigPath string
	Verbose    bool
	Interfaces []networking.Interface
}

// loadAndValidateConfigOrFail loads configuration from file and validates it.
// This performs structural validation only. Interface validation should be done
// separately using networking.ValidateInterfacesArePresent() where needed.
func loadAndValidateConfigOrFail(configPath string) (*config.Config, error) {
	cfg, err := config.LoadConfig(configPath)
	if err != nil {
		return nil, fmt.Errorf("failed to load configuration: %v", err)
	}

	// Validate configuration structure (includes prefilling default iptables rules)
	if err := cfg.ValidateConfig(); err != nil {
		return nil, fmt.Errorf("configuration validation failed: %v", err)
	}

	return cfg, nil
}
