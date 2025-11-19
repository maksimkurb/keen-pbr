package commands

import (
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
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

func loadAndValidateConfigOrFail(configPath string) (*config.Config, error) {
	cfg, err := config.LoadConfig(configPath)
	if err != nil {
		return nil, fmt.Errorf("failed to load configuration: %v", err)
	}

	// Call config's own validation first (includes prefilling default iptables rules)
	if err := cfg.ValidateConfig(); err != nil {
		return nil, fmt.Errorf("configuration validation failed: %v", err)
	}

	// Validate configuration using ValidationService
	validator := service.NewValidationService()
	if err = validator.ValidateConfig(cfg); err != nil {
		return nil, fmt.Errorf("configuration validation is failed: %v", err)
	}
	return cfg, nil
}
