package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// IPRuleComponent wraps an IP rule and implements the NetworkingComponent interface.
// IP rules route packets marked by iptables to custom routing tables.
type IPRuleComponent struct {
	ComponentBase
	rule *IPRule
	cfg  *config.IPSetConfig
}

// NewIPRuleComponent creates a new IP rule component from configuration
func NewIPRuleComponent(cfg *config.IPSetConfig) *IPRuleComponent {
	rule := BuildIPRuleFromConfig(cfg)
	return &IPRuleComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPRule,
			description:   "IP rule routes packets marked by iptables to the custom routing table",
		},
		rule: rule,
		cfg:  cfg,
	}
}

// IsExists checks if the IP rule currently exists in the system
func (c *IPRuleComponent) IsExists() (bool, error) {
	return c.rule.IsExists()
}

// ShouldExist returns true because IP rules should always exist for configured ipsets
func (c *IPRuleComponent) ShouldExist() bool {
	return true
}

// CreateIfNotExists creates the IP rule if it doesn't already exist
func (c *IPRuleComponent) CreateIfNotExists() error {
	_, err := c.rule.AddIfNotExists()
	return err
}

// DeleteIfExists removes the IP rule if it exists
func (c *IPRuleComponent) DeleteIfExists() error {
	_, err := c.rule.DelIfExists()
	return err
}

// GetRule returns the underlying IP rule
func (c *IPRuleComponent) GetRule() *IPRule {
	return c.rule
}
