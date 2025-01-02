package config

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"

	"github.com/BurntSushi/toml"
)

var (
	ipsetRegexp = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)
)

type IpFamily uint8

const (
	Ipv4 IpFamily = 4
	Ipv6 IpFamily = 6
)

type Config struct {
	General *GeneralConfig `toml:"general"`
	Ipset   []*IpsetConfig `toml:"ipset"`
}

type GeneralConfig struct {
	IpsetPath       string `toml:"ipset_path"`
	ListsOutputDir  string `toml:"lists_output_dir"`
	DnsmasqListsDir string `toml:"dnsmasq_lists_dir"`
	Summarize       bool   `toml:"summarize"`
}

type IpsetConfig struct {
	IpsetName           string         `toml:"ipset_name"`
	IpVersion           IpFamily       `toml:"ip_version"`
	FlushBeforeApplying bool           `toml:"flush_before_applying"`
	Routing             *RoutingConfig `toml:"routing"`
	List                []*ListSource  `toml:"list"`
}

type RoutingConfig struct {
	Interface      string   `toml:"interface"`
	Interfaces     []string `toml:"interfaces"`
	FwMark         uint32   `toml:"fwmark"`
	IpRouteTable   int      `toml:"table"`
	IpRulePriority int      `toml:"priority"`
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

	return &config, nil
}

func (c *Config) ValidateConfig() error {
	names := make(map[string]bool)
	fwmarks := make(map[uint32]bool)
	tables := make(map[int]bool)
	priorities := make(map[int]bool)

	for _, ipset := range c.Ipset {
		// Validate ipset name
		if err := validateIpsetName(ipset.IpsetName); err != nil {
			return err
		}
		if err := checkDuplicates(ipset.IpsetName, names, "ipset name"); err != nil {
			return err
		}

		// Validate IP version
		newVersion, err := validateIpVersion(ipset.IpVersion)
		if err != nil {
			return err
		}
		ipset.IpVersion = newVersion

		// Validate interfaces
		if ipset.Routing.Interface == "" && len(ipset.Routing.Interfaces) == 0 {
			return fmt.Errorf("ipset %s routing configuration should contain \"interfaces\" field, check your configuration", ipset.IpsetName)
		}
		if ipset.Routing.Interface != "" && len(ipset.Routing.Interfaces) > 0 {
			return fmt.Errorf("ipset %s contains both \"interface\" and \"interfaces\" fields, please use only one field to configure routing", ipset.IpsetName)
		}
		if ipset.Routing.Interface != "" {
			ipset.Routing.Interfaces = []string{ipset.Routing.Interface}
		}
		// check duplicate interfaces
		ifNames := make(map[string]bool)
		for _, ifName := range ipset.Routing.Interfaces {
			if ifNames[ifName] {
				return fmt.Errorf("ipset %s contains duplicate interface \"%s\", check your configuration", ipset.IpsetName, ifName)
			}
			ifNames[ifName] = true
		}

		// Validate routing configuration using generic duplicate checker
		if err := checkDuplicates(ipset.Routing.FwMark, fwmarks, "fwmark"); err != nil {
			return err
		}
		if err := checkDuplicates(ipset.Routing.IpRouteTable, tables, "table"); err != nil {
			return err
		}
		if err := checkDuplicates(ipset.Routing.IpRulePriority, priorities, "priority"); err != nil {
			return err
		}

		// Validate lists
		listNames := make(map[string]bool)
		for _, list := range ipset.List {
			if err := validateList(list, listNames, ipset.IpsetName); err != nil {
				return err
			}
		}
	}
	return nil
}

func checkDuplicates[T comparable](value T, seen map[T]bool, itemType string) error {
	if seen[value] {
		return fmt.Errorf("duplicate %s found: %v, check your configuration", itemType, value)
	}
	seen[value] = true
	return nil
}

func validateNonEmpty(value, fieldName string) error {
	if value == "" {
		return fmt.Errorf("%s cannot be empty, check your configuration", fieldName)
	}
	return nil
}

func validateIpVersion(version IpFamily) (IpFamily, error) {
	if version != Ipv6 {
		if version != Ipv4 && version != 0 {
			return 0, fmt.Errorf("unknown IP version %d, check your configuration", version)
		}
		return Ipv4, nil
	}
	return Ipv6, nil
}

// Validate list configuration
func validateList(list *ListSource, listNames map[string]bool, ipsetName string) error {
	if err := validateNonEmpty(list.ListName, "list name"); err != nil {
		return err
	}

	if err := checkDuplicates(list.ListName, listNames, "list name"); err != nil {
		return fmt.Errorf("%v in ipset %s", err, ipsetName)
	}

	if list.URL == "" && (list.Hosts == nil || len(list.Hosts) == 0) {
		return fmt.Errorf("list %s should contain URL or hosts list, check your configuration", list.ListName)
	}

	if list.URL != "" && (list.Hosts != nil && len(list.Hosts) > 0) {
		return fmt.Errorf("list %s can contain either URL or hosts list, not both, check your configuration", list.ListName)
	}

	return nil
}

func validateIpsetName(ipsetName string) error {
	if err := validateNonEmpty(ipsetName, "ipset name"); err != nil {
		return err
	}
	if !ipsetRegexp.MatchString(ipsetName) {
		return fmt.Errorf("ipset name should consist only of lowercase [a-z0-9_], check your configuration")
	}
	return nil
}
