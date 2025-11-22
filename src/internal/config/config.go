package config

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"

	"github.com/pelletier/go-toml/v2"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

var (
	ipsetRegexp = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)
)

type IPFamily uint8

const (
	Ipv4 IPFamily = 4
	Ipv6 IPFamily = 6
)

const (
	IPTablesTmplIpset    = "ipset_name"
	IPTablesTmplFwmark   = "fwmark"
	IPTablesTmplTable    = "table"
	IPTablesTmplPriority = "priority"
)

func LoadConfig(configPath string) (*Config, error) {
	configFile := filepath.Clean(configPath)

	if !filepath.IsAbs(configFile) {
		if path, err := filepath.Abs(configFile); err != nil {
			return nil, fmt.Errorf("failed to get absolute path: %v", err)
		} else {
			configFile = path
		}
	}

	if _, err := os.Stat(configFile); os.IsNotExist(err) {
		parentDir := filepath.Dir(configFile)
		if err := os.MkdirAll(parentDir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create parent directory: %v", err)
		}
		log.Errorf("Configuration file not found: %s", configFile)
		return nil, fmt.Errorf("configuration file not found: %s", configFile)
	}

	content, err := os.ReadFile(configFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %v", err)
	}

	var config Config
	if err := toml.Unmarshal(content, &config); err != nil {
		var derr *toml.DecodeError
		if errors.As(err, &derr) {
			log.Errorf(derr.String())
			row, col := derr.Position()
			log.Errorf("Error at line %d, column %d", row, col)
			return nil, fmt.Errorf("failed to parse config file")
		}
		return nil, fmt.Errorf("failed to parse config file: %v", err)
	}

	config._absConfigFilePath = configFile

	log.Debugf("Configuration file path: %s", configFile)
	log.Debugf("Downloaded lists directory: %s", config.GetAbsDownloadedListsDir())

	return &config, nil
}

func (c *Config) SerializeConfig() (*bytes.Buffer, error) {
	buf := bytes.Buffer{}
	enc := toml.NewEncoder(&buf)
	enc.SetIndentTables(true)
	if err := enc.Encode(c); err != nil {
		return nil, err
	}
	return &buf, nil
}

func (c *Config) WriteConfig() error {
	config, err := c.SerializeConfig()
	if err != nil {
		return err
	}
	if err := os.WriteFile(c._absConfigFilePath, config.Bytes(), 0644); err != nil {
		return err
	}
	return nil
}

func (c *Config) UpgradeConfig() (bool, error) {
	upgraded := false

	for _, ipset := range c.IPSets {
		if ipset.IPVersion == 0 {
			ipset.IPVersion = Ipv4

			log.Infof("Upgrading required field \"ip_version\" for ipset %s", ipset.IPSetName)
			upgraded = true
		}
	}

	return upgraded, nil
}
