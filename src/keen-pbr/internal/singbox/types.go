package singbox

// Config represents the complete sing-box configuration
type Config struct {
	Log          LogConfig          `json:"log"`
	DNS          DNSConfig          `json:"dns"`
	NTP          map[string]any     `json:"ntp"`
	Certificate  map[string]any     `json:"certificate"`
	Endpoints    []any              `json:"endpoints"`
	Inbounds     []Inbound          `json:"inbounds"`
	Outbounds    []interface{}      `json:"outbounds"`
	Route        RouteConfig        `json:"route"`
	Services     []any              `json:"services"`
	Experimental ExperimentalConfig `json:"experimental"`
}

// LogConfig represents logging configuration
type LogConfig struct {
	Disabled  bool   `json:"disabled"`
	Level     string `json:"level"`
	Timestamp bool   `json:"timestamp"`
}

// DNSConfig represents DNS configuration
type DNSConfig struct {
	Servers          []DNSServer `json:"servers"`
	Rules            []DNSRule   `json:"rules"`
	Final            string      `json:"final"`
	Strategy         string      `json:"strategy"`
	IndependentCache bool        `json:"independent_cache"`
}

// DNSServer represents a DNS server configuration
type DNSServer struct {
	Type           string `json:"type"`
	Tag            string `json:"tag"`
	Server         string `json:"server"`
	ServerPort     int    `json:"server_port,omitempty"`
	Detour         string `json:"detour,omitempty"`
	DomainResolver string `json:"domain_resolver,omitempty"`
	Inet4Range     string `json:"inet4_range,omitempty"`
}

// DNSRule represents a DNS routing rule
type DNSRule struct {
	Action       string   `json:"action,omitempty"`
	QueryType    string   `json:"query_type,omitempty"`
	DomainSuffix string   `json:"domain_suffix,omitempty"`
	Server       string   `json:"server,omitempty"`
	RewriteTTL   int      `json:"rewrite_ttl,omitempty"`
	Domain       []string `json:"domain,omitempty"`
	RuleSet      []string `json:"rule_set,omitempty"`
}

// Inbound represents an inbound connection handler
type Inbound struct {
	Type        string `json:"type"`
	Tag         string `json:"tag"`
	Listen      string `json:"listen"`
	ListenPort  int    `json:"listen_port"`
	TCPFastOpen bool   `json:"tcp_fast_open,omitempty"`
	UDPFragment bool   `json:"udp_fragment,omitempty"`
}

// Outbound represents an outbound connection handler
type Outbound struct {
	Type           string `json:"type"`
	Tag            string `json:"tag"`
	BindInterface  string `json:"bind_interface,omitempty"`
	DomainResolver string `json:"domain_resolver,omitempty"`
}

// RouteConfig represents routing configuration
type RouteConfig struct {
	Rules                 []RouteRule `json:"rules"`
	RuleSet               []RuleSet   `json:"rule_set"`
	Final                 string      `json:"final"`
	AutoDetectInterface   bool        `json:"auto_detect_interface"`
	DefaultDomainResolver string      `json:"default_domain_resolver"`
}

// RouteRule represents a routing rule
type RouteRule struct {
	Action       string   `json:"action"`
	Inbound      any      `json:"inbound,omitempty"` // can be string or []string
	Outbound     string   `json:"outbound,omitempty"`
	Protocol     string   `json:"protocol,omitempty"`
	Domain       string   `json:"domain,omitempty"`
	RuleSet      any      `json:"rule_set,omitempty"` // can be string or []string
	OverridePort int      `json:"override_port,omitempty"`
}

// RuleSet represents a rule set configuration
type RuleSet struct {
	Type           string `json:"type"`
	Tag            string `json:"tag"`
	Format         string `json:"format"`
	URL            string `json:"url,omitempty"`
	Path           string `json:"path,omitempty"`
	UpdateInterval string `json:"update_interval,omitempty"`
}

// ExperimentalConfig represents experimental features configuration
type ExperimentalConfig struct {
	CacheFile CacheFileConfig `json:"cache_file"`
	ClashAPI  ClashAPIConfig  `json:"clash_api"`
}

// CacheFileConfig represents cache file configuration
type CacheFileConfig struct {
	Enabled     bool   `json:"enabled"`
	Path        string `json:"path"`
	StoreFakeIP bool   `json:"store_fakeip"`
}

// ClashAPIConfig represents Clash API configuration
type ClashAPIConfig struct {
	ExternalController string `json:"external_controller"`
	ExternalUI         string `json:"external_ui"`
}
