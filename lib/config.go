package lib

import (
	"fmt"
	"log"
	"os"
	"path/filepath"

	"github.com/BurntSushi/toml"
)

type Config struct {
	General GeneralConfig `toml:"general"`
	List    []ListSource  `toml:"list"`
}

type GeneralConfig struct {
	IpsetPath      string `toml:"ipset_path"`
	ListsOutputDir string `toml:"lists_output_dir"`
	DnsmasqConfDir string `toml:"dnsmasq_conf_dir"`
	Summarize      bool   `toml:"summarize"`
}

type ListSource struct {
	Name  string `toml:"name"`
	URL   string `toml:"url"`
	Ipset string `toml:"ipset"`
}

func LoadConfig(configPath string) (*Config, error) {
	configFile := filepath.Clean(configPath)

	if _, err := os.Stat(configFile); os.IsNotExist(err) {
		parentDir := filepath.Dir(configFile)
		if err := os.MkdirAll(parentDir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create parent directory: %v", err)
		}
		log.Printf("Configuration file not found: %s", configFile)
		return nil, fmt.Errorf("configuration file not found: %s", configFile)
	}

	content, err := os.ReadFile(configFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %v", err)
	}

	var config Config
	if err := toml.Unmarshal(content, &config); err != nil {
		return nil, fmt.Errorf("failed to parse config file: %v", err)
	}

	if err := config.validateListNamesUnique(); err != nil {
		return nil, err
	}

	return &config, nil
}

func (c *Config) validateListNamesUnique() error {
	names := make(map[string]bool)
	for _, list := range c.List {
		if names[list.Name] {
			return fmt.Errorf("duplicate list name found: %s, check your configuration", list.Name)
		}
		names[list.Name] = true
	}
	return nil
}
