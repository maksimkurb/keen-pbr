package networking

import (
	"github.com/coreos/go-iptables/iptables"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// IPTablesBuilder provides a fluent interface for building IPTableRules.
//
// This builder encapsulates the logic for creating iptables rules from
// an ipset configuration, handling protocol selection and rule processing.
type IPTablesBuilder struct {
	ipset *config.IPSetConfig
}

// NewIPTablesBuilder creates a new builder for IPTableRules.
//
// The builder will construct iptables rules based on the provided ipset
// configuration, including proper protocol selection (IPv4 vs IPv6).
func NewIPTablesBuilder(ipset *config.IPSetConfig) *IPTablesBuilder {
	return &IPTablesBuilder{ipset: ipset}
}

// Build constructs and returns IPTableRules for the configured ipset.
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
func (b *IPTablesBuilder) Build() (*IPTableRules, error) {
	protocol := iptables.ProtocolIPv4
	if b.ipset.IPVersion == config.Ipv6 {
		protocol = iptables.ProtocolIPv6
	}

	ipt, err := iptables.NewWithProtocol(protocol)
	if err != nil {
		return nil, err
	}

	rules, err := processRules(b.ipset)
	if err != nil {
		return nil, err
	}

	return &IPTableRules{ipt, b.ipset, rules}, nil
}

// IPRuleBuilder provides a fluent interface for building IpRule.
//
// This builder encapsulates the logic for creating IP routing rules from
// an ipset configuration, extracting the necessary parameters for rule creation.
type IPRuleBuilder struct {
	ipset *config.IPSetConfig
}

// NewIPRuleBuilder creates a new builder for IpRule.
//
// The builder will construct an IP rule based on the provided ipset
// configuration, extracting routing parameters like fwmark, table, and priority.
func NewIPRuleBuilder(ipset *config.IPSetConfig) *IPRuleBuilder {
	return &IPRuleBuilder{ipset: ipset}
}

// Build constructs and returns an IpRule for the configured ipset.
//
// The build process extracts the following from ipset configuration:
// - IP family (IPv4 or IPv6)
// - Firewall mark (fwmark) for packet matching
// - Routing table ID
// - Rule priority
//
// Returns a fully configured IpRule instance ready for addition/removal.
func (b *IPRuleBuilder) Build() *IpRule {
	return BuildRule(
		b.ipset.IPVersion,
		b.ipset.Routing.FwMark,
		b.ipset.Routing.IpRouteTable,
		b.ipset.Routing.IpRulePriority,
	)
}
