package networking

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// IPTablesRuleComponent wraps a single IPTables rule and implements the NetworkingComponent interface.
// IPTables rules mark packets matching the ipset with fwmark for policy routing.
type IPTablesRuleComponent struct {
	ComponentBase
	rules     *IPTableRules
	ruleIndex int
	cfg       *config.IPSetConfig
}

// NewIPTablesRuleComponents creates IPTablesRuleComponent instances for all rules in an ipset
func NewIPTablesRuleComponents(cfg *config.IPSetConfig) ([]*IPTablesRuleComponent, error) {
	rules, err := BuildIPTablesRules(cfg)
	if err != nil {
		return nil, fmt.Errorf("failed to build iptables rules: %w", err)
	}

	components := []*IPTablesRuleComponent{}
	for idx := range rules.rules {
		components = append(components, &IPTablesRuleComponent{
			ComponentBase: ComponentBase{
				ipsetName:     cfg.IPSetName,
				componentType: ComponentTypeIPTables,
				description:   "IPTables rule marks packets matching the ipset with fwmark for policy routing",
			},
			rules:     rules,
			ruleIndex: idx,
			cfg:       cfg,
		})
	}
	return components, nil
}

// IsExists checks if this specific IPTables rule currently exists in the system
func (c *IPTablesRuleComponent) IsExists() (bool, error) {
	existsMap, err := c.rules.CheckRulesExists()
	if err != nil {
		return false, err
	}
	rule := c.rules.rules[c.ruleIndex]
	return existsMap[rule], nil
}

// ShouldExist returns true because IPTables rules should always exist for configured ipsets
func (c *IPTablesRuleComponent) ShouldExist() bool {
	return true
}

// CreateIfNotExists creates this specific IPTables rule if it doesn't already exist
func (c *IPTablesRuleComponent) CreateIfNotExists() error {
	exists, err := c.IsExists()
	if err != nil {
		return fmt.Errorf("failed to check if iptables rule exists: %w", err)
	}
	if exists {
		return nil
	}

	rule := c.rules.rules[c.ruleIndex]
	if err := c.rules.ipt.Append(rule.Table, rule.Chain, rule.Rule...); err != nil {
		return fmt.Errorf("failed to add iptables rule: %w", err)
	}
	return nil
}

// DeleteIfExists removes this specific IPTables rule if it exists
func (c *IPTablesRuleComponent) DeleteIfExists() error {
	exists, err := c.IsExists()
	if err != nil {
		return fmt.Errorf("failed to check if iptables rule exists: %w", err)
	}
	if !exists {
		return nil
	}

	rule := c.rules.rules[c.ruleIndex]
	if err := c.rules.ipt.Delete(rule.Table, rule.Chain, rule.Rule...); err != nil {
		return fmt.Errorf("failed to delete iptables rule: %w", err)
	}
	return nil
}

// GetRuleIndex returns the index of this rule within the IPTablesRules collection
func (c *IPTablesRuleComponent) GetRuleIndex() int {
	return c.ruleIndex
}

// GetRuleDescription returns a human-readable description of this rule
func (c *IPTablesRuleComponent) GetRuleDescription() string {
	rule := c.rules.rules[c.ruleIndex]
	return fmt.Sprintf("%s/%s rule #%d", rule.Table, rule.Chain, c.ruleIndex+1)
}
