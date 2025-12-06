package keenetic

const rciPrefix = "http://127.0.0.1:79/rci"

// Interface represents the network interface details
type Interface struct {
	ID          string        `json:"id"`
	Address     string        `json:"address"`
	Mask        string        `json:"mask"`
	IPv6        InterfaceIPv6 `json:"ipv6"`
	Type        string        `json:"type"`
	Description string        `json:"description"`
	Link        string        `json:"link"`
	Connected   string        `json:"connected"`
	State       string        `json:"state"`
	SystemName  string        `json:"system-name,omitempty"` // Linux interface name, populated manually
}

type InterfaceIPv6 struct {
	Addresses []InterfaceIPv6Address `json:"addresses"`
}

type InterfaceIPv6Address struct {
	Address      string `json:"address"`
	PrefixLength int    `json:"prefix-length"`
}

type Interfaces map[string]Interface

const (
	KeeneticLinkUp    = "up"
	KeeneticConnected = "yes"
)

type DNSServerType string

const (
	DNSServerTypePlain     DNSServerType = "IP4"
	DNSServerTypePlainIPv6 DNSServerType = "IP6"
	DNSServerTypeDoT       DNSServerType = "DoT"
	DNSServerTypeDoH       DNSServerType = "DoH"
)

type DNSServerInfo struct {
	Type     DNSServerType
	Domain   *string
	Proxy    string
	Endpoint string // For DoT: SNI, for DoH: URI, for Plain/PlainIPv6: same as Proxy
	Port     string // Only for DoT and DoH entries, empty otherwise
}

// DNSProxyResponse represents the response from /show/dns-proxy endpoint
type DNSProxyResponse struct {
	ProxyStatus []DNSProxyStatus `json:"proxy-status"`
}

// DNSProxyStatus represents a single DNS proxy profile
type DNSProxyStatus struct {
	ProxyName   string `json:"proxy-name"`
	ProxyConfig string `json:"proxy-config"`
	// Other fields omitted as we only need proxy-name and proxy-config
}
