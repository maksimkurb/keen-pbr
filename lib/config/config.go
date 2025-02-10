package config

import (
	"bytes"
	"errors"
	"fmt"
	"github.com/pelletier/go-toml/v2"
	"os"
	"path/filepath"
	"regexp"
	"slices"

	"github.com/maksimkurb/keenetic-pbr/lib/log"
)

var (
	ipsetRegexp = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)
)

type IpFamily uint8

const (
	Ipv4 IpFamily = 4
	Ipv6 IpFamily = 6
)

const (
	IPTABLES_TMPL_IPSET    = "ipset_name"
	IPTABLES_TMPL_FWMARK   = "fwmark"
	IPTABLES_TMPL_TABLE    = "table"
	IPTABLES_TMPL_PRIORITY = "priority"
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
		if ipset.Routing.DeprecatedInterface != "" {
			if !slices.Contains(ipset.Routing.Interfaces, ipset.Routing.DeprecatedInterface) {
				ipset.Routing.Interfaces = append(ipset.Routing.Interfaces, ipset.Routing.DeprecatedInterface)
			}
			ipset.Routing.DeprecatedInterface = ""

			log.Infof("Upgrading deprecated field \"interface\" to \"interfaces\" for ipset %s", ipset.IPSetName)
			upgraded = true
		}

		if ipset.DeprecatedLists != nil {
			for _, list := range ipset.DeprecatedLists {
				newListName := ipset.IPSetName + "-" + list.Name()

				if !slices.Contains(ipset.Lists, newListName) {
					ipset.Lists = append(ipset.Lists, newListName)
				}

				list.ListName = newListName

				c.Lists = append(c.Lists, list)
			}

			ipset.DeprecatedLists = nil

			log.Infof("Upgrading deprecated field \"ipset.list\" to \"list\" for ipset %s", ipset.IPSetName)
			upgraded = true
		}

		if ipset.IPVersion == 0 {
			ipset.IPVersion = Ipv4

			log.Infof("Upgrading required field \"ip_version\" for ipset %s", ipset.IPSetName)
			upgraded = true
		}
	}

	for _, list := range c.Lists {
		if list.DeprecatedName != "" {
			if list.ListName == "" {
				list.ListName = list.DeprecatedName
			}
			list.DeprecatedName = ""

			log.Infof("Upgrading deprecated field \"name\" to \"list_name\" for list %s", list.Name())
			upgraded = true
		}
	}

	return upgraded, nil
}
