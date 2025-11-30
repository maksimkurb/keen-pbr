package config

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

type Config struct {
	// ConfigVersion is the configuration file version.
	ConfigVersion uint8 `toml:"config_version" json:"config_version"`
	// General holds general configuration.
	General *GeneralConfig `toml:"general"`
	// IPSets is the ipset configuration. You can add multiple ipsets.
	IPSets []*IPSetConfig `toml:"ipset,omitempty"`
	// Lists contains lists with domains/IPs/CIDRs. You can add multiple lists and use them in ipsets by providing their name. You must set "name" and either "url", "file" or "hosts" field for each list.
	Lists []*ListSource `toml:"list,omitempty"`

	_absConfigFilePath string
}

type GeneralConfig struct {
	// ListsOutputDir is the directory for downloaded lists.
	ListsOutputDir string `toml:"lists_output_dir" json:"lists_output_dir" validate:"required"`
	// InterfaceMonitoringIntervalSeconds is the interval in seconds for interface monitoring (0 = disabled, default: 0).
	InterfaceMonitoringIntervalSeconds int `toml:"interface_monitoring_interval_seconds" json:"interface_monitoring_interval_seconds" validate:"gte=0"`

	// AutoUpdate lists settings
	AutoUpdate *AutoUpdateConfig `toml:"auto_update_lists" json:"auto_update_lists"`

	// DNS Server settings
	DNSServer *DNSServerConfig `toml:"dns_server" json:"dns_server"`
}

type AutoUpdateConfig struct {
	// Enabled enables automatic background updates of lists with URLs (default: true).
	Enabled bool `toml:"enabled" json:"enabled"`
	// IntervalHours is the interval in hours for automatic list updates (default: 24 hours, min: 1 hour).
	IntervalHours int `toml:"interval_hours" json:"interval_hours" validate:"min=0,required_if=Enabled true,gte=1"`
}

type DNSServerConfig struct {
	// Enable enables transparent DNS proxy for domain-based routing (default: true).
	Enable bool `toml:"enable" json:"enable"`
	// ListenAddr is the DNS proxy listen address (default: [::] for dual-stack IPv4/IPv6, or a specific IP like 127.0.0.1).
	ListenAddr string `toml:"listen_addr" json:"listen_addr" validate:"ip_or_empty"`
	// ListenPort is the port for DNS proxy listener (default: 15353).
	ListenPort uint16 `toml:"listen_port" json:"listen_port" validate:"required,min=1"`
	// Upstreams lists upstream DNS servers. Supported: keenetic://, udp://ip:port, doh://host/path (default: ["keenetic://"]).
	Upstreams []string `toml:"upstreams" json:"upstreams" validate:"dive,upstream_url,required_if=Enable true"`
	// CacheMaxDomains is the maximum number of domains to cache in DNS proxy (default: 1000).
	CacheMaxDomains int `toml:"cache_max_domains" json:"cache_max_domains" validate:"min=0"`
	// DropAAAA drops AAAA (IPv6) DNS responses (default: true).
	DropAAAA bool `toml:"drop_aaaa" json:"drop_aaaa"`
	// IPSetEntryAdditionalTTLSec is added to DNS record TTL to determine IPSet entry lifetime in seconds (default: 7200 = 2 hours).
	IPSetEntryAdditionalTTLSec uint32 `toml:"ipset_entry_additional_ttl_sec" json:"ipset_entry_additional_ttl_sec" validate:"min=0,max=2147483"`
	// ListedDomainsDNSCacheTTLSec is the TTL used for DNS cache entries of domains matched by lists (default: 30). Allows clients to forget DNS fast for listed domains. Other domains keep original TTL.
	ListedDomainsDNSCacheTTLSec uint32 `toml:"listed_domains_dns_cache_ttl_sec" json:"listed_domains_dns_cache_ttl_sec" validate:"min=0,max=2147483"`
	// Remap53Interfaces are interfaces to intercept DNS traffic on (default: ["br0", "br1"]).
	Remap53Interfaces []string `toml:"remap_53_interfaces" json:"remap_53_interfaces" validate:"required_if=Enable true"`
}

type IPSetConfig struct {
	// IPSetName is the name of the ipset.
	IPSetName string `toml:"ipset_name" json:"ipset_name" validate:"required,ipset_name"`
	// Lists adds all hosts from the following lists to this ipset.
	Lists []string `toml:"lists" json:"lists" validate:"required,min=1"`
	// IPVersion is the IP version (4 or 6).
	IPVersion IPFamily `toml:"ip_version" json:"ip_version" validate:"required,oneof=4 6"`
	// FlushBeforeApplying clears ipset each time before filling it.
	FlushBeforeApplying bool           `toml:"flush_before_applying" json:"flush_before_applying"`
	Routing             *RoutingConfig `toml:"routing" json:"routing,omitempty" validate:"required"`
	// IPTablesRules are iptables rules for this ipset (you can provide multiple rules). Available variables: {{ipset_name}}, {{fwmark}}, {{table}}, {{priority}}.
	IPTablesRules []*IPTablesRule `toml:"iptables_rule,omitempty" json:"iptables_rule,omitempty" validate:"dive"`
}

type IPTablesRule struct {
	Chain string   `toml:"chain" json:"chain"`
	Table string   `toml:"table" json:"table"`
	Rule  []string `toml:"rule" json:"rule"`
}

type RoutingConfig struct {
	// Interfaces is the list of interfaces to direct traffic for IPs in this ipset. keen-pbr will use the first available interface. Keenetic API will be queried automatically to check network connectivity on interfaces. If all interfaces are down, traffic will be blocked (blackhole route) or allowed to leak based on kill_switch setting.
	Interfaces []string `toml:"interfaces" json:"interfaces"`
	// DefaultGateway is the default gateway IP address to use instead of interface-based routing. Must match the IP version of the ipset (IPv4 for ipv4, IPv6 for ipv6). If set, this gateway will be used when no interface is available or no interfaces are configured.
	DefaultGateway string `toml:"default_gateway" json:"default_gateway,omitempty" validate:"ip_or_empty"`
	// KillSwitch defines kill switch behavior when all interfaces are down. If true (default): traffic is blocked via blackhole route (no leaks). If false: ip rules and iptables rules are removed, allowing traffic to use default routing (leaks allowed).
	KillSwitch bool `toml:"kill_switch" json:"kill_switch"`
	// FwMark is the fwmark to apply to packets matching the list criteria.
	FwMark uint32 `toml:"fwmark" json:"fwmark" validate:"required,min=1"`
	// IPRouteTable is the iptables routing table number.
	IPRouteTable int `toml:"table" json:"table" validate:"required,min=1"`
	// IPRulePriority is the iptables routing rule priority.
	IPRulePriority int `toml:"priority" json:"priority" validate:"required,min=1"`
	// DNS settings for this routing rule
	DNS *RoutingDNSConfig `toml:"dns" json:"dns,omitempty"`
}

type RoutingDNSConfig struct {
	// Upstreams overrides DNS server for domains in this ipset. Format: <server>[#port] (e.g., 1.1.1.1#53 or 8.8.8.8).
	Upstreams []string `toml:"upstreams" json:"upstreams" validate:"dive,upstream_url"`
}

type ListSource struct {
	// ListName is the name of the list.
	ListName string `toml:"list_name" json:"list_name" validate:"required"`
	// URL is the URL of the list (optional).
	URL string `toml:"url,omitempty" json:"url,omitempty" validate:"omitempty,url"`
	// File is the local file path of the list (optional).
	File string `toml:"file,omitempty" json:"file,omitempty"`
	// Hosts is a list of host entries for the list (optional).
	Hosts []string `toml:"hosts,omitempty" json:"hosts,omitempty"`
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
	return lst.ListName
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
