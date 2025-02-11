package commands

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
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

	// Validate configuration
	if err = cfg.ValidateConfig(); err != nil {
		return nil, fmt.Errorf("configuration validation is failed: %v", err)
	}
	return cfg, nil
}
