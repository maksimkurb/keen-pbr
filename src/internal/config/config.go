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
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
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
			log.Errorf("%s", derr.String())
			row, col := derr.Position()
			log.Errorf("Error at line %d, column %d", row, col)
			return nil, fmt.Errorf("failed to parse config file")
		}
		return nil, fmt.Errorf("failed to parse config file: %v", err)
	}

	config._absConfigFilePath = configFile

	log.Debugf("Configuration file path: %s", configFile)
	log.Debugf("Downloaded lists directory: %s", utils.GetAbsolutePath(config.General.ListsOutputDir, filepath.Dir(configFile)))

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

	// Check if config needs version upgrade (version 0 or < 3)
	if c.ConfigVersion < 3 {
		log.Infof("Upgrading config from version %d to version 3", c.ConfigVersion)

		// Ensure General config exists
		if c.General == nil {
			c.General = &GeneralConfig{}
		}

		// Set default values for General config fields if they are empty/zero
		if c.General.ListsOutputDir == "" {
			c.General.ListsOutputDir = "/opt/etc/keen-pbr/lists.d"
			log.Infof("Setting default lists_output_dir: %s", c.General.ListsOutputDir)
		}

		// Set default values for new config version 0
		if c.ConfigVersion == 0 {
			// Ensure General sub-configs are initialized
			// Ensure sub-configs are initialized
			if c.General == nil {
				c.General = &GeneralConfig{}
			}
			if c.General.AutoUpdate == nil {
				c.General.AutoUpdate = &AutoUpdateConfig{}
			}
			if c.General.DNSServer == nil {
				c.General.DNSServer = &DNSServerConfig{}
			}
			// Set defaults
			c.General.InterfaceMonitoringIntervalSeconds = 0 // Disabled by default
			c.General.AutoUpdate.Enabled = true
			c.General.DNSServer.Enable = true
			c.General.DNSServer.DropAAAA = true
			c.General.DNSServer.Remap53Interfaces = []string{"br0", "br1"}
			log.Infof("Setting default values for new config version 0")
		}

		// Set default int values
		if c.General.AutoUpdate != nil {
			if c.General.AutoUpdate.IntervalHours <= 0 {
				c.General.AutoUpdate.IntervalHours = 24
				log.Infof("Setting default update_interval_hours: %d", c.General.AutoUpdate.IntervalHours)
			} else if c.General.AutoUpdate.IntervalHours < 1 {
				c.General.AutoUpdate.IntervalHours = 1
				log.Infof("Adjusting update_interval_hours to minimum: 1")
			}
		}

		if c.General.DNSServer.ListenPort == 0 {
			c.General.DNSServer.ListenPort = 15353
			log.Infof("Setting default dns_proxy_port: %d", c.General.DNSServer.ListenPort)
		}

		if c.General.DNSServer.CacheMaxDomains == 0 {
			c.General.DNSServer.CacheMaxDomains = 1000
			log.Infof("Setting default dns_cache_max_domains: %d", c.General.DNSServer.CacheMaxDomains)
		}

		if c.General.DNSServer.ListedDomainsDNSCacheTTLSec == 0 {
			c.General.DNSServer.ListedDomainsDNSCacheTTLSec = 30
			log.Infof("Setting default listed_domains_dns_cache_ttl_sec: %d", c.General.DNSServer.ListedDomainsDNSCacheTTLSec)
		}

		if c.General.DNSServer.IPSetEntryAdditionalTTLSec == 0 {
			c.General.DNSServer.IPSetEntryAdditionalTTLSec = 7200
			log.Infof("Setting default ipset_entry_additional_ttl_sec: %d", c.General.DNSServer.IPSetEntryAdditionalTTLSec)
		}

		// Set default string values
		if c.General.DNSServer.ListenAddr == "" {
			c.General.DNSServer.ListenAddr = "[::]"
			log.Infof("Setting default dns_proxy_listen_addr: %s", c.General.DNSServer.ListenAddr)
		}

		// Set default slice values
		if len(c.General.DNSServer.Upstreams) == 0 {
			c.General.DNSServer.Upstreams = []string{"keenetic://"}
			log.Infof("Setting default dns_upstream: %v", c.General.DNSServer.Upstreams)
		}

		if len(c.General.DNSServer.Remap53Interfaces) == 0 {
			c.General.DNSServer.Remap53Interfaces = []string{"br0", "br1"}
			log.Infof("Setting default dns_proxy_interfaces: %v", c.General.DNSServer.Remap53Interfaces)
		}

		// Upgrade ipsets
		for _, ipset := range c.IPSets {
			// Set default IP version if not set
			if ipset.IPVersion == 0 {
				ipset.IPVersion = Ipv4
				log.Infof("Setting default ip_version for ipset %s: 4", ipset.IPSetName)
			}

			// Set default KillSwitch value for routing config
			if ipset.Routing != nil && c.ConfigVersion == 0 {
				ipset.Routing.KillSwitch = true
				log.Infof("Setting default kill_switch for ipset %s: true", ipset.IPSetName)
			}
		}

		// Update version to 3
		c.ConfigVersion = 3
		upgraded = true
		log.Infof("Config upgraded to version 3")
	}

	return upgraded, nil
}
