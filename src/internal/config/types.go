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
	ListsOutputDir            string `toml:"lists_output_dir" json:"lists_output_dir" comment:"Directory for downloaded lists"`
	UseKeeneticDNS            *bool  `toml:"use_keenetic_dns" json:"use_keenetic_dns" comment:"Use Keenetic DNS from System profile as upstream"`
	FallbackDNS               string `toml:"fallback_dns" json:"fallback_dns" comment:"Fallback DNS server to use if Keenetic RCI call fails (e.g. 8.8.8.8 or 1.1.1.1)"`
	APIBindAddress            string `toml:"api_bind_address" json:"api_bind_address" comment:"API server bind address (e.g. 0.0.0.0:8080). Access is restricted to private subnets only."`
	AutoUpdateLists           *bool  `toml:"auto_update_lists" json:"auto_update_lists" comment:"Automatically update lists with URLs in background (default: true)"`
	UpdateIntervalHours       int    `toml:"update_interval_hours" json:"update_interval_hours" comment:"Interval in hours for automatic list updates (default: 24 hours, min: 1 hour)"`
	EnableInterfaceMonitoring *bool  `toml:"enable_interface_monitoring" json:"enable_interface_monitoring" comment:"Enable periodic interface status monitoring in web UI (default: false)"`

	// DNS Proxy settings
	EnableDNSProxy     *bool    `toml:"enable_dns_proxy" json:"enable_dns_proxy" comment:"Enable transparent DNS proxy for domain-based routing (default: true)"`
	DNSProxyListenAddr string   `toml:"dns_proxy_listen_addr" json:"dns_proxy_listen_addr" comment:"DNS proxy listen address (default: [::] for dual-stack IPv4/IPv6, or use specific IP like 127.0.0.1)"`
	DNSProxyPort       int      `toml:"dns_proxy_port" json:"dns_proxy_port" comment:"Port for DNS proxy listener (default: 15353)"`
	DNSUpstream        []string `toml:"dns_upstream" json:"dns_upstream" comment:"Upstream DNS servers. Supported: keenetic://, udp://ip:port, doh://host/path (default: [\"keenetic://\"])"`
	DropAAAA           *bool    `toml:"drop_aaaa" json:"drop_aaaa" comment:"Drop AAAA (IPv6) DNS responses (default: true)"`
	TTLOverride        int      `toml:"ttl_override" json:"ttl_override" comment:"Override TTL for DNS responses in seconds (0 = use original TTL)"`
}

// IsAutoUpdateEnabled returns whether auto-update is enabled (default: true).
func (gc *GeneralConfig) IsAutoUpdateEnabled() bool {
	if gc.AutoUpdateLists == nil {
		return true // Default to enabled
	}
	return *gc.AutoUpdateLists
}

// GetUpdateIntervalHours returns the update interval in hours (default: 24, min: 1).
func (gc *GeneralConfig) GetUpdateIntervalHours() int {
	if gc.UpdateIntervalHours <= 0 {
		return 24 // Default to 24 hours
	}
	if gc.UpdateIntervalHours < 1 {
		return 1 // Minimum 1 hour
	}
	return gc.UpdateIntervalHours
}

// IsInterfaceMonitoringEnabled returns whether interface monitoring is enabled (default: false).
func (gc *GeneralConfig) IsInterfaceMonitoringEnabled() bool {
	if gc.EnableInterfaceMonitoring == nil {
		return false // Default to disabled
	}
	return *gc.EnableInterfaceMonitoring
}

// IsDNSProxyEnabled returns whether DNS proxy is enabled (default: true).
func (gc *GeneralConfig) IsDNSProxyEnabled() bool {
	if gc.EnableDNSProxy == nil {
		return true // Default to enabled
	}
	return *gc.EnableDNSProxy
}

// GetDNSProxyListenAddr returns the DNS proxy listen address (default: [::]).
func (gc *GeneralConfig) GetDNSProxyListenAddr() string {
	if gc.DNSProxyListenAddr == "" {
		return "[::]" // Default to dual-stack with brackets
	}
	return gc.DNSProxyListenAddr
}

// GetDNSProxyPort returns the DNS proxy port (default: 15353).
func (gc *GeneralConfig) GetDNSProxyPort() int {
	if gc.DNSProxyPort <= 0 {
		return 15353 // Default port
	}
	return gc.DNSProxyPort
}

// GetDNSProxyHost returns the DNS proxy IPv4 host (default: 127.0.53.53).
func (gc *GeneralConfig) GetDNSProxyHost() string {
	return "127.0.53.53"
}

// GetDNSProxyHostIPv6 returns the DNS proxy IPv6 host (default: ::1).
func (gc *GeneralConfig) GetDNSProxyHostIPv6() string {
	return "::1"
}

// GetDNSUpstream returns the configured upstream DNS servers (default: ["keenetic://"]).
func (gc *GeneralConfig) GetDNSUpstream() []string {
	if len(gc.DNSUpstream) == 0 {
		return []string{"keenetic://"}
	}
	return gc.DNSUpstream
}

// IsDropAAAAEnabled returns whether AAAA (IPv6) responses should be dropped (default: true).
func (gc *GeneralConfig) IsDropAAAAEnabled() bool {
	if gc.DropAAAA == nil {
		return true // Default to dropping AAAA
	}
	return *gc.DropAAAA
}

// GetTTLOverride returns the TTL override value (0 = use original TTL).
func (gc *GeneralConfig) GetTTLOverride() uint32 {
	if gc.TTLOverride < 0 {
		return 0
	}
	return uint32(gc.TTLOverride)
}

type IPSetConfig struct {
	IPSetName           string          `toml:"ipset_name" json:"ipset_name" comment:"Name of the ipset."`
	Lists               []string        `toml:"lists" json:"lists" comment:"Add all hosts from the following lists to this ipset."`
	IPVersion           IPFamily        `toml:"ip_version" json:"ip_version" comment:"IP version (4 or 6)"`
	FlushBeforeApplying bool            `toml:"flush_before_applying" json:"flush_before_applying" comment:"Clear ipset each time before filling it"`
	Routing             *RoutingConfig  `toml:"routing" json:"routing,omitempty"`
	IPTablesRules       []*IPTablesRule `toml:"iptables_rule,omitempty" json:"iptables_rule,omitempty" comment:"An iptables rule for this ipset (you can provide multiple rules).\nAvailable variables: {{ipset_name}}, {{fwmark}}, {{table}}, {{priority}}."`
}

type IPTablesRule struct {
	Chain string   `toml:"chain" json:"chain"`
	Table string   `toml:"table" json:"table"`
	Rule  []string `toml:"rule" json:"rule"`
}

type RoutingConfig struct {
	Interfaces     []string `toml:"interfaces" json:"interfaces" comment:"Interface list to direct traffic for IPs in this ipset to.\nkeen-pbr will use first available interface.\nKeenetic API will be queried automatically to check network connectivity on interfaces.\nIf all interfaces are down, traffic will be blocked (blackhole route) or allowed to leak based on kill_switch setting."`
	DefaultGateway string   `toml:"default_gateway" json:"default_gateway,omitempty" comment:"Default gateway IP address to use instead of interface-based routing.\nMust match the IP version of the ipset (IPv4 for ipv4, IPv6 for ipv6).\nIf set, this gateway will be used when no interface is available or no interfaces are configured."`
	KillSwitch     *bool    `toml:"kill_switch" json:"kill_switch,omitempty" comment:"Kill switch behavior when all interfaces are down.\nIf true (default): traffic is blocked via blackhole route (no leaks).\nIf false: ip rules and iptables rules are removed, allowing traffic to use default routing (leaks allowed)."`
	FwMark         uint32   `toml:"fwmark" json:"fwmark" comment:"Fwmark to apply to packets matching the list criteria."`
	IPRouteTable   int      `toml:"table" json:"table" comment:"iptables routing table number"`
	IPRulePriority int      `toml:"priority" json:"priority" comment:"iptables routing rule priority"`
	DNSOverride    string   `toml:"override_dns" json:"override_dns,omitempty" comment:"Override DNS server for domains in this ipset. Format: <server>[#port] (e.g. 1.1.1.1#53 or 8.8.8.8)"`
}

type ListSource struct {
	ListName string   `toml:"list_name" json:"list_name"`
	URL      string   `toml:"url,omitempty" json:"url,omitempty"`
	File     string   `toml:"file,omitempty" json:"file,omitempty"`
	Hosts    []string `toml:"hosts,multiline,omitempty" json:"hosts,omitempty"`
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

// IsKillSwitchEnabled returns whether kill switch is enabled (default: true).
// When enabled, traffic is blocked via blackhole route when all interfaces are down.
// When disabled, ip rules and iptables rules are removed, allowing traffic to leak.
func (rc *RoutingConfig) IsKillSwitchEnabled() bool {
	if rc.KillSwitch == nil {
		return true // Default to enabled for safety
	}
	return *rc.KillSwitch
}
