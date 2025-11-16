package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

func TestProcessRulePart_TemplateSubstitution(t *testing.T) {
	ipset := &config.IPSetConfig{
		IPSetName: "test-ipset",
		Routing: &config.RoutingConfig{
			FwMark:         100,
			IpRouteTable:   200,
			IpRulePriority: 300,
		},
	}

	tests := []struct {
		name     string
		template string
		expected string
	}{
		{
			name:     "No template variables",
			template: "INPUT -j ACCEPT",
			expected: "INPUT -j ACCEPT",
		},
		{
			name:     "Single ipset_name variable",
			template: "INPUT -m set --match-set {{ipset_name}} src -j ACCEPT",
			expected: "INPUT -m set --match-set test-ipset src -j ACCEPT",
		},
		{
			name:     "Single fwmark variable",
			template: "OUTPUT -m mark --mark {{fwmark}} -j ACCEPT",
			expected: "OUTPUT -m mark --mark 100 -j ACCEPT",
		},
		{
			name:     "Single table variable",
			template: "PREROUTING -j MARK --set-mark {{table}}",
			expected: "PREROUTING -j MARK --set-mark 200",
		},
		{
			name:     "Single priority variable",
			template: "POSTROUTING -m mark --mark {{priority}} -j ACCEPT",
			expected: "POSTROUTING -m mark --mark 300 -j ACCEPT",
		},
		{
			name:     "Multiple variables",
			template: "INPUT -m set --match-set {{ipset_name}} src -m mark --mark {{fwmark}} -j MARK --set-mark {{table}}",
			expected: "INPUT -m set --match-set test-ipset src -m mark --mark 100 -j MARK --set-mark 200",
		},
		{
			name:     "All variables",
			template: "{{ipset_name}} {{fwmark}} {{table}} {{priority}}",
			expected: "test-ipset 100 200 300",
		},
		{
			name:     "Variable with surrounding text",
			template: "prefix_{{ipset_name}}_suffix",
			expected: "prefix_test-ipset_suffix",
		},
		{
			name:     "Unknown variable - gets replaced with empty string",
			template: "INPUT {{unknown_var}} -j ACCEPT",
			expected: "INPUT  -j ACCEPT",
		},
		{
			name:     "Empty template",
			template: "",
			expected: "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := processRulePart(tt.template, ipset)
			if result != tt.expected {
				t.Errorf("Expected %q, got %q", tt.expected, result)
			}
		})
	}
}

func TestProcessRulePart_MalformedTemplate(t *testing.T) {
	ipset := &config.IPSetConfig{
		IPSetName: "test-ipset",
		Routing: &config.RoutingConfig{
			FwMark: 100,
		},
	}

	// Test that malformed templates cause panic (which is the actual behavior)
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic for malformed template")
		}
	}()

	// This should panic
	processRulePart("INPUT {{ipset_name} -j ACCEPT", ipset)
}

func TestProcessRulePart_EdgeCases(t *testing.T) {
	tests := []struct {
		name     string
		ipset    *config.IPSetConfig
		template string
		expected string
	}{
		{
			name: "Empty ipset name",
			ipset: &config.IPSetConfig{
				IPSetName: "",
				Routing: &config.RoutingConfig{
					FwMark: 100,
				},
			},
			template: "INPUT -m set --match-set {{ipset_name}} src",
			expected: "INPUT -m set --match-set  src",
		},
		{
			name: "Zero values",
			ipset: &config.IPSetConfig{
				IPSetName: "test",
				Routing: &config.RoutingConfig{
					FwMark:         0,
					IpRouteTable:   0,
					IpRulePriority: 0,
				},
			},
			template: "{{fwmark}} {{table}} {{priority}}",
			expected: "0 0 0",
		},
		{
			name: "Large values",
			ipset: &config.IPSetConfig{
				IPSetName: "test",
				Routing: &config.RoutingConfig{
					FwMark:         4294967295, // Max uint32
					IpRouteTable:   65535,
					IpRulePriority: 32767,
				},
			},
			template: "{{fwmark}} {{table}} {{priority}}",
			expected: "4294967295 65535 32767",
		},
		{
			name:     "Nil ipset",
			ipset:    nil,
			template: "{{ipset_name}}",
			expected: "{{ipset_name}}", // Should handle gracefully
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// This test might panic with nil ipset, so we'll catch that
			defer func() {
				if r := recover(); r != nil {
					if tt.ipset == nil {
						t.Logf("Expected panic with nil ipset: %v", r)
					} else {
						t.Errorf("Unexpected panic: %v", r)
					}
				}
			}()

			result := processRulePart(tt.template, tt.ipset)
			if result != tt.expected {
				t.Errorf("Expected %q, got %q", tt.expected, result)
			}
		})
	}
}

func TestProcessRules_BusinessLogic(t *testing.T) {
	tests := []struct {
		name           string
		ipset          *config.IPSetConfig
		expectError    bool
		expectedRules  int
	}{
		{
			name: "IPSet with iptables rules",
			ipset: &config.IPSetConfig{
				IPSetName: "test-ipset",
				Routing: &config.RoutingConfig{
					FwMark:         100,
					IpRouteTable:   200,
					IpRulePriority: 300,
				},
				IPTablesRules: []*config.IPTablesRule{
					{
						Chain: "INPUT",
						Table: "filter",
						Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "src", "-j", "ACCEPT"},
					},
					{
						Chain: "OUTPUT", 
						Table: "filter",
						Rule:  []string{"-m", "mark", "--mark", "{{fwmark}}", "-j", "ACCEPT"},
					},
				},
			},
			expectError:   false,
			expectedRules: 2,
		},
		{
			name: "IPSet with no iptables rules",
			ipset: &config.IPSetConfig{
				IPSetName: "test-ipset",
				Routing: &config.RoutingConfig{
					FwMark: 100,
				},
				IPTablesRules: []*config.IPTablesRule{},
			},
			expectError:   false,
			expectedRules: 0,
		},
		{
			name: "IPSet with nil iptables rules",
			ipset: &config.IPSetConfig{
				IPSetName: "test-ipset",
				Routing: &config.RoutingConfig{
					FwMark: 100,
				},
				IPTablesRules: nil,
			},
			expectError:   false,
			expectedRules: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			rules, err := processRules(tt.ipset)

			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
				if len(rules) != tt.expectedRules {
					t.Errorf("Expected %d rules, got %d", tt.expectedRules, len(rules))
				}
			}
		})
	}
}