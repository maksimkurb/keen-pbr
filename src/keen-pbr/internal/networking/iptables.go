package networking

import (
	"fmt"
	"os/exec"
	"strings"
)

// IPTablesRule represents a single iptables rule
type IPTablesRule struct {
	Table string
	Chain string
	Rule  []string
}

// IPTablesManager manages iptables rules
type IPTablesManager struct {
	rules []*IPTablesRule
}

// NewIPTablesManager creates a new iptables manager
func NewIPTablesManager() *IPTablesManager {
	return &IPTablesManager{
		rules: []*IPTablesRule{},
	}
}

// AddRule adds a rule to the manager
func (m *IPTablesManager) AddRule(table, chain string, rule ...string) {
	m.rules = append(m.rules, &IPTablesRule{
		Table: table,
		Chain: chain,
		Rule:  rule,
	})
}

// ruleExists checks if a rule exists
func (m *IPTablesManager) ruleExists(rule *IPTablesRule) bool {
	args := []string{"-t", rule.Table, "-C", rule.Chain}
	args = append(args, rule.Rule...)

	cmd := exec.Command("iptables", args...)
	return cmd.Run() == nil
}

// ApplyRules applies all rules
func (m *IPTablesManager) ApplyRules() error {
	for _, rule := range m.rules {
		if m.ruleExists(rule) {
			continue
		}

		args := []string{"-t", rule.Table, "-A", rule.Chain}
		args = append(args, rule.Rule...)

		cmd := exec.Command("iptables", args...)
		output, err := cmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("failed to add iptables rule %v: %w, output: %s",
				rule, err, string(output))
		}
	}
	return nil
}

// RemoveRules removes all rules
func (m *IPTablesManager) RemoveRules() error {
	// Remove in reverse order
	for i := len(m.rules) - 1; i >= 0; i-- {
		rule := m.rules[i]

		if !m.ruleExists(rule) {
			continue
		}

		args := []string{"-t", rule.Table, "-D", rule.Chain}
		args = append(args, rule.Rule...)

		cmd := exec.Command("iptables", args...)
		output, err := cmd.CombinedOutput()
		if err != nil {
			// Log but don't fail if rule doesn't exist
			if !strings.Contains(string(output), "does a matching rule exist") {
				return fmt.Errorf("failed to remove iptables rule %v: %w, output: %s",
					rule, err, string(output))
			}
		}
	}
	return nil
}

// String returns a string representation of the rule
func (r *IPTablesRule) String() string {
	return fmt.Sprintf("%s/%s: %s", r.Table, r.Chain, strings.Join(r.Rule, " "))
}
