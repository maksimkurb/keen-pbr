package config

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

type Config struct {
	General *GeneralConfig `toml:"general"`
	IPSets  []*IPSetConfig `toml:"ipset,omitempty" comment:"ipset configuration.\nYou can add multiple ipsets."`
	Lists   []*ListSource  `toml:"list,omitempty" comment:"Lists with domains/IPs/CIDRs.\nYou can add multiple lists and use them in ipsets by providing their name.\nYou must set \"name\" and either \"url\", \"file\" or \"hosts\" field for each list."`

	_absConfigFilePath string
}

type GeneralConfig struct {
	ListsOutputDir string `toml:"lists_output_dir" comment:"Directory for downloaded lists"`
	UseKeeneticAPI *bool  `toml:"use_keenetic_api" comment:"Use Keenetic RCI API to check network connection availability on the interface"`
	UseKeeneticDNS *bool  `toml:"use_keenetic_dns" comment:"Use Keenetic DNS from System profile as upstream in generated dnsmasq config"`
	FallbackDNS    string `toml:"fallback_dns" comment:"Fallback DNS server to use if Keenetic RCI call fails (e.g. 8.8.8.8 or 1.1.1.1)"`
}

type IPSetConfig struct {
	IPSetName           string          `toml:"ipset_name" comment:"Name of the ipset."`
	Lists               []string        `toml:"lists" comment:"Add all hosts from the following lists to this ipset."`
	IPVersion           IpFamily        `toml:"ip_version" comment:"IP version (4 or 6)"`
	FlushBeforeApplying bool            `toml:"flush_before_applying" comment:"Clear ipset each time before filling it"`
	Routing             *RoutingConfig  `toml:"routing"`
	IPTablesRules       []*IPTablesRule `toml:"iptables_rule,omitempty" comment:"An iptables rule for this ipset (you can provide multiple rules).\nAvailable variables: {{ipset_name}}, {{fwmark}}, {{table}}, {{priority}}."`

	DeprecatedLists []*ListSource `toml:"list,omitempty"`
}

type IPTablesRule struct {
	Chain string   `toml:"chain"`
	Table string   `toml:"table"`
	Rule  []string `toml:"rule"`
}

type RoutingConfig struct {
	Interfaces     []string `toml:"interfaces" comment:"Interface list to direct traffic for IPs in this ipset to.\nkeen-pbr will use first available interface.\nIf use_keenetic_api is enabled, keen-pbr will also check if there is any network connectivity on this interface."`
	KillSwitch     bool     `toml:"kill_switch" comment:"Drop all traffic to the hosts from this ipset if all interfaces are down (prevent traffic leaks)."`
	FwMark         uint32   `toml:"fwmark" comment:"Fwmark to apply to packets matching the list criteria."`
	IpRouteTable   int      `toml:"table" comment:"iptables routing table number"`
	IpRulePriority int      `toml:"priority" comment:"iptables routing rule priority"`
	DNSOverride    string   `toml:"override_dns" comment:"Override DNS server for domains in this ipset. Format: <server>[#port] (e.g. 1.1.1.1#53 or 8.8.8.8)"`

	DeprecatedInterface string `toml:"interface,omitempty"`
}

type ListSource struct {
	ListName string   `toml:"list_name"`
	URL      string   `toml:"url,omitempty"`
	File     string   `toml:"file,omitempty"`
	Hosts    []string `toml:"hosts,multiline,omitempty"`

	DeprecatedName string `toml:"name,omitempty"`
}

func (c *Config) GetConfigDir() string {
	return filepath.Dir(c._absConfigFilePath)
}

func (c *Config) GetAbsDownloadedListsDir() string {
	return utils.GetAbsolutePath(c.General.ListsOutputDir, c.GetConfigDir())
}

func (lst *ListSource) Type() string {
	if lst.URL != "" {
		return "url"
	} else if lst.File != "" {
		return "file"
	} else {
		return "hosts"
	}
}

func (lst *ListSource) Name() string {
	if lst.ListName != "" {
		return lst.ListName
	} else if lst.DeprecatedName != "" {
		return lst.DeprecatedName
	} else {
		return ""
	}
}

func (lst *ListSource) GetAbsolutePath(cfg *Config) (string, error) {
	var path string
	if lst.URL != "" {
		path = filepath.Join(cfg.GetAbsDownloadedListsDir(), fmt.Sprintf("%s.lst", lst.ListName))
	} else if lst.File != "" {
		path = utils.GetAbsolutePath(lst.File, cfg.GetConfigDir())
	} else if lst.Hosts != nil {
		return "", fmt.Errorf("list is not a file")
	}

	if path == "" {
		return "", fmt.Errorf("list path is empty")
	}

	return path, nil
}

func (lst *ListSource) GetAbsolutePathAndCheckExists(cfg *Config) (string, error) {
	if path, err := lst.GetAbsolutePath(cfg); err != nil {
		return "", err
	} else {
		if _, err := os.Stat(path); errors.Is(err, os.ErrNotExist) {
			if lst.URL != "" {
				return "", fmt.Errorf("list file is not exists: %s, please run 'keen-pbr download' first", path)
			} else {
				return "", fmt.Errorf("list file is not exists: %s", path)
			}
		}

		return path, nil
	}
}
