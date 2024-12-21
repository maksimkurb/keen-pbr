package lib

import (
	"fmt"
	"github.com/BurntSushi/toml"
	"log"
	"os"
	"path/filepath"
)

type IpFamily uint8

const (
	Ipv4 IpFamily = 4
	Ipv6 IpFamily = 6
)

type Config struct {
	General GeneralConfig `toml:"general"`
	Ipset   []IpsetConfig `toml:"ipset"`
}

type GeneralConfig struct {
	IpsetPath       string `toml:"ipset_path"`
	ListsOutputDir  string `toml:"lists_output_dir"`
	DnsmasqListsDir string `toml:"dnsmasq_lists_dir"`
	Summarize       bool   `toml:"summarize"`
}

type IpsetConfig struct {
	IpsetName           string        `toml:"ipset_name"`
	IpVersion           IpFamily      `toml:"ip_version"`
	FlushBeforeApplying bool          `toml:"flush_before_applying"`
	Routing             RoutingConfig `toml:"routing"`
	List                []ListSource  `toml:"list"`
}

type RoutingConfig struct {
	Interface      string `toml:"interface"`
	FwMark         uint16 `toml:"fwmark"`
	IpRouteTable   uint16 `toml:"table"`
	IpRulePriority uint16 `toml:"priority"`
}

type ListSource struct {
	ListName string   `toml:"name"`
	URL      string   `toml:"url"`
	File     string   `toml:"file"`
	Hosts    []string `toml:"hosts"`
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
		if parseErr, ok := err.(toml.ParseError); ok {
			log.Printf("%s", parseErr.ErrorWithUsage())
			return nil, fmt.Errorf("failed to parse config file")
		}
		return nil, fmt.Errorf("failed to parse config file: %v", err)
	}
	if err := config.validateConfig(); err != nil {
		return nil, err
	}

	return &config, nil
}

func (c *Config) validateConfig() error {
	names := make(map[string]bool)
	fwmarks := make(map[uint16]bool)
	tables := make(map[uint16]bool)
	priorities := make(map[uint16]bool)
	for _, ipset := range c.Ipset {
		if ipset.IpsetName == "" {
			return fmt.Errorf("ipset name cannot be empty, check your configuration")
		}
		if names[ipset.IpsetName] {
			return fmt.Errorf("duplicate ipset name found: %s, check your configuration", ipset.IpsetName)
		}
		names[ipset.IpsetName] = true

		if ipset.IpVersion != Ipv6 {
			if ipset.IpVersion != Ipv4 && ipset.IpVersion != 0 {
				return fmt.Errorf("unknown IP version %d, check your configuration", ipset.IpVersion)
			}
			ipset.IpVersion = Ipv4
		}

		if ipset.Routing.Interface == "" {
			return fmt.Errorf("interface cannot be empty, check your configuration")
		}
		if fwmarks[ipset.Routing.FwMark] {
			return fmt.Errorf("duplicate fwmark found: %s, check your configuration", ipset.Routing.FwMark)
		}
		fwmarks[ipset.Routing.FwMark] = true

		if tables[ipset.Routing.IpRouteTable] {
			return fmt.Errorf("duplicate table found: %s, check your configuration", ipset.Routing.IpRouteTable)
		}
		tables[ipset.Routing.IpRouteTable] = true

		if priorities[ipset.Routing.IpRulePriority] {
			return fmt.Errorf("duplicate priority found: %s, check your configuration", ipset.Routing.IpRulePriority)
		}
		priorities[ipset.Routing.IpRulePriority] = true

		list_names := make(map[string]bool)
		for _, list := range ipset.List {
			if list.ListName == "" {
				return fmt.Errorf("list name cannot be empty, check your configuration")
			}
			if list.URL == "" && (list.Hosts == nil || len(list.Hosts) == 0) {
				return fmt.Errorf("list %s should contain URL or hosts list, check your configuration")
			}
			if list.URL != "" && (list.Hosts != nil && len(list.Hosts) > 0) {
				return fmt.Errorf("list %s can contain either URL or hosts list, not both, check your configuration")
			}

			if list_names[list.ListName] {
				return fmt.Errorf("duplicate list name found: %s in ipset %s, check your configuration", list.ListName, ipset.IpsetName)
			}
			list_names[list.ListName] = true

		}
	}
	return nil
}

func GenRoutingConfig(c *Config, ipFamily IpFamily) error {
	for _, ipset := range c.Ipset {
		if ipset.IpVersion != ipFamily {
			continue
		}
		fmt.Printf("kpbr_ipset=%s kpbr_interface=%s kpbr_fwmark=%d kpbr_table_id=%d kpbr_rule_priority=%d\n", ipset.IpsetName, ipset.Routing.Interface, ipset.Routing.FwMark, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)
	}
	return nil
}
