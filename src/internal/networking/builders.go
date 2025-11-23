package networking

import (
	"github.com/coreos/go-iptables/iptables"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// BuildIPTablesRules constructs IPTableRules for the specified ipset configuration.
//
// The build process:
// 1. Determines the correct iptables protocol (IPv4 or IPv6) from ipset config
// 2. Creates an iptables instance with the appropriate protocol
// 3. Processes rule templates from ipset configuration
// 4. Returns a fully configured IPTableRules instance
//
// Returns an error if:
// - The iptables instance cannot be created
// - Rule templates cannot be processed
func BuildIPTablesRules(ipset *config.IPSetConfig) (*IPTableRules, error) {
	protocol := iptables.ProtocolIPv4
	if ipset.IPVersion == config.Ipv6 {
		protocol = iptables.ProtocolIPv6
	}

	ipt, err := iptables.NewWithProtocol(protocol)
	if err != nil {
		return nil, err
	}

	rules := processRules(ipset)

	return &IPTableRules{ipt, ipset, rules}, nil
}

// BuildIPRuleFromConfig constructs an IP rule for the specified ipset configuration.
//
// The build process extracts the following from ipset configuration:
// - IP family (IPv4 or IPv6)
// - Firewall mark (fwmark) for packet matching
// - Routing table ID
// - Rule priority
//
// Returns a fully configured IPRule instance ready for addition/removal.
func BuildIPRuleFromConfig(ipset *config.IPSetConfig) *IPRule {
	return BuildRule(
		ipset.IPVersion,
		ipset.Routing.FwMark,
		ipset.Routing.IPRouteTable,
		ipset.Routing.IPRulePriority,
	)
}
