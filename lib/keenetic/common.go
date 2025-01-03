package keenetic

const rciPrefix = "http://localhost:79/rci"

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
}

type InterfaceIPv6 struct {
	Addresses []InterfaceIPv6Address `json:"addresses"`
}

type InterfaceIPv6Address struct {
	Address      string `json:"address"`
	PrefixLength int    `json:"prefix-length"`
}

type Interfaces map[string]Interface
