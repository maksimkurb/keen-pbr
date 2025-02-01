package config

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"

	"github.com/BurntSushi/toml"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
)

var (
	ipsetRegexp = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)
)

type IpFamily uint8

const (
	Ipv4  IpFamily = 4
	Ipv6  IpFamily = 6
	IpAll IpFamily = 46
)

const (
	IPTABLES_TMPL_IPSET    = "ipset_name"
	IPTABLES_TMPL_FWMARK   = "fwmark"
	IPTABLES_TMPL_TABLE    = "table"
	IPTABLES_TMPL_PRIORITY = "priority"
)

type Config struct {
	General         *GeneralConfig `toml:"general"`
	IPSets          []*IPSetConfig `toml:"ipset"`
	_configFilePath string
}

type GeneralConfig struct {
	ListsOutputDir  string `toml:"lists_output_dir"`
	DnsmasqListsDir string `toml:"dnsmasq_lists_dir"`
	UseKeeneticAPI  *bool  `toml:"use_keenetic_api"`
}

type IPSetConfig struct {
	IPSetName           string          `toml:"ipset_name"`
	IPVersion           IpFamily        `toml:"ip_version"`
	FlushBeforeApplying bool            `toml:"flush_before_applying"`
	Routing             *RoutingConfig  `toml:"routing"`
	IPTablesRules       []*IPTablesRule `toml:"iptables_rule"`
	Lists               []*ListSource   `toml:"list"`
}

type IPTablesRule struct {
	Chain string   `toml:"chain"`
	Table string   `toml:"table"`
	Rule  []string `toml:"rule"`
}

type RoutingConfig struct {
	Interface      string   `toml:"interface,omitempty"`
	Interfaces     []string `toml:"interfaces"`
	KillSwitch     bool     `toml:"kill_switch"`
	FwMark         uint32   `toml:"fwmark"`
	IpRouteTable   int      `toml:"table"`
	IpRulePriority int      `toml:"priority"`
}

type ListSource struct {
	ListName string   `toml:"name"`
	URL      string   `toml:"url,omitempty"`
	File     string   `toml:"file,omitempty"`
	Hosts    []string `toml:"hosts,omitempty"`
}

func (lst ListSource) Type() string {
	if lst.URL != "" {
		return "url"
	} else if lst.File != "" {
		return "file"
	} else {
		return "hosts"
	}
}

func LoadConfig(configPath string) (*Config, error) {
	configFile := filepath.Clean(configPath)

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
		if parseErr, ok := err.(toml.ParseError); ok {
			log.Errorf("%s", parseErr.ErrorWithUsage())
			return nil, fmt.Errorf("failed to parse config file")
		}
		return nil, fmt.Errorf("failed to parse config file: %v", err)
	}

	config._configFilePath = configFile

	return &config, nil
}

func (c *Config) SerializeConfig() ([]byte, error) {
	if bytes, err := toml.Marshal(c); err != nil {
		return nil, err
	} else {
		return bytes, nil
	}
}

func (c *Config) ValidateConfig() error {
	if c.General == nil {
		return fmt.Errorf("configuration should contain \"general\" field, check your configuration")
	}

	if c.General.UseKeeneticAPI == nil {
		def := true
		c.General.UseKeeneticAPI = &def
	}

	if c.IPSets == nil {
		return fmt.Errorf("configuration should contain \"ipset\" field, check your configuration")
	}

	names := make(map[string]bool)
	fwmarks := make(map[uint32]bool)
	tables := make(map[int]bool)
	priorities := make(map[int]bool)

	for _, ipset := range c.IPSets {
		if ipset.Routing == nil {
			return fmt.Errorf("ipset %s should contain \"ipset.routing\" field, check your configuration", ipset.IPSetName)
		}

		if ipset.Lists == nil {
			return fmt.Errorf("ipset %s should contain \"ipset.list\" field, check your configuration", ipset.IPSetName)
		}

		// Validate ipset name
		if err := validateIpsetName(ipset.IPSetName); err != nil {
			return err
		}
		if err := checkDuplicates(ipset.IPSetName, names, "ipset name"); err != nil {
			return err
		}

		// Validate IP version
		if newVersion, err := validateIpVersion(ipset.IPVersion); err != nil {
			return err
		} else {
			ipset.IPVersion = newVersion
		}

		// Validate iptables rules
		if err := validateIPTablesRules(ipset); err != nil {
			return err
		}

		// Validate interfaces
		if ipset.Routing.Interface == "" && len(ipset.Routing.Interfaces) == 0 {
			return fmt.Errorf("ipset %s routing configuration should contain \"interfaces\" field, check your configuration", ipset.IPSetName)
		}
		if ipset.Routing.Interface != "" && len(ipset.Routing.Interfaces) > 0 {
			return fmt.Errorf("ipset %s contains both \"interface\" and \"interfaces\" fields, please use only one field to configure routing", ipset.IPSetName)
		}
		if ipset.Routing.Interface != "" {
			log.Warnf("ipset %s contains deprecated \"interface\" field, please use \"interfaces\" instead", ipset.IPSetName)
			ipset.Routing.Interfaces = []string{ipset.Routing.Interface}
			ipset.Routing.Interface = ""
		}
		// check duplicate interfaces
		ifNames := make(map[string]bool)
		for _, ifName := range ipset.Routing.Interfaces {
			if ifNames[ifName] {
				return fmt.Errorf("ipset %s contains duplicate interface \"%s\", check your configuration", ipset.IPSetName, ifName)
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
		for _, list := range ipset.Lists {
			if err := validateList(c, list, listNames, ipset.IPSetName); err != nil {
				return err
			}
		}
	}
	return nil
}

func validateIPTablesRules(ipset *IPSetConfig) error {
	if ipset.IPTablesRules == nil {
		ipset.IPTablesRules = []*IPTablesRule{
			{
				Chain: "PREROUTING",
				Table: "mangle",
				Rule: []string{
					"-m", "set", "--match-set", "{{" + IPTABLES_TMPL_IPSET + "}}", "dst,src",
					"-j", "MARK", "--set-mark", "{{" + IPTABLES_TMPL_FWMARK + "}}",
				},
			},
		}

		return nil
	}

	if len(ipset.IPTablesRules) > 0 {
		for _, rule := range ipset.IPTablesRules {
			if rule.Chain == "" {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"chain\" field, check your configuration", ipset.IPSetName)
			}
			if rule.Table == "" {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"table\" field, check your configuration", ipset.IPSetName)
			}
			if len(rule.Rule) == 0 {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"rule\" field, check your configuration", ipset.IPSetName)
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
func validateList(cfg *Config, list *ListSource, listNames map[string]bool, ipsetName string) error {
	if err := validateNonEmpty(list.ListName, "list name"); err != nil {
		return err
	}

	if err := checkDuplicates(list.ListName, listNames, "list name"); err != nil {
		return fmt.Errorf("%v in ipset %s", err, ipsetName)
	}

	isUrl := list.URL != ""
	isFile := list.File != ""
	isHosts := list.Hosts != nil && len(list.Hosts) > 0

	if !isUrl && !isFile && !isHosts {
		return fmt.Errorf("list %s should contain \"url\", \"file\" or non-empty \"hosts\" field, check your configuration", list.ListName)
	}

	if (isUrl && (isFile || isHosts)) || (isFile && isHosts) {
		return fmt.Errorf("list %s can contain only one of \"url\", \"file\" or \"hosts\" field, but not both, check your configuration", list.ListName)
	}

	if isFile {
		list.File = makePathAbsolute(list.File, filepath.Dir(cfg._configFilePath))
		if _, err := os.Stat(list.File); errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("list %s file \"%s\" does not exist, check your configuration", list.ListName, list.File)
		}
	}

	return nil
}

func makePathAbsolute(path, baseDir string) string {
	// Check if the path is already absolute
	if filepath.IsAbs(path) {
		return path
	}

	// Join the relative path with the config directory
	absolutePath := filepath.Join(baseDir, path)

	// Clean the resulting path
	absolutePath = filepath.Clean(absolutePath)

	return absolutePath
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
