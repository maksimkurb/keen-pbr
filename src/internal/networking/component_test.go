package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// TestIPSetComponent_ShouldExist tests that IPSet components should always exist
func TestIPSetComponent_ShouldExist(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
	}

	component := NewIPSetComponent(cfg)

	if !component.ShouldExist() {
		t.Error("IPSet component should always return true for ShouldExist()")
	}
}

// TestIPSetComponent_GetType tests that IPSet component returns correct type
func TestIPSetComponent_GetType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
	}

	component := NewIPSetComponent(cfg)

	if component.GetType() != ComponentTypeIPSet {
		t.Errorf("Expected type %s, got %s", ComponentTypeIPSet, component.GetType())
	}
}

// TestIPSetComponent_GetIPSetName tests that component returns correct IPSet name
func TestIPSetComponent_GetIPSetName(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
	}

	component := NewIPSetComponent(cfg)

	if component.GetIPSetName() != "test-ipset" {
		t.Errorf("Expected IPSet name 'test-ipset', got '%s'", component.GetIPSetName())
	}
}

// TestIPSetComponent_GetDescription tests that component returns description
func TestIPSetComponent_GetDescription(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
	}

	component := NewIPSetComponent(cfg)

	desc := component.GetDescription()
	if desc == "" {
		t.Error("Expected non-empty description")
	}
	if desc != "IPSet must exist to store IP addresses/subnets for policy routing" {
		t.Errorf("Unexpected description: %s", desc)
	}
}

// TestIPRuleComponent_ShouldExist tests that IPRule components should always exist
func TestIPRuleComponent_ShouldExist(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IpRouteTable:   100,
			IpRulePriority: 100,
		},
	}

	component := NewIPRuleComponent(cfg)

	if !component.ShouldExist() {
		t.Error("IPRule component should always return true for ShouldExist()")
	}
}

// TestIPRuleComponent_GetType tests that IPRule component returns correct type
func TestIPRuleComponent_GetType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IpRouteTable:   100,
			IpRulePriority: 100,
		},
	}

	component := NewIPRuleComponent(cfg)

	if component.GetType() != ComponentTypeIPRule {
		t.Errorf("Expected type %s, got %s", ComponentTypeIPRule, component.GetType())
	}
}

// TestIPRouteComponent_GetType tests that IPRoute component returns correct type
func TestIPRouteComponent_GetType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			IpRouteTable: 100,
			Interfaces:   []string{"eth0"},
		},
	}

	selector := NewInterfaceSelector(nil)
	component := NewBlackholeRouteComponent(cfg, selector)

	if component.GetType() != ComponentTypeIPRoute {
		t.Errorf("Expected type %s, got %s", ComponentTypeIPRoute, component.GetType())
	}
}

// TestIPRouteComponent_BlackholeType tests that blackhole route has correct type
func TestIPRouteComponent_BlackholeType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			IpRouteTable: 100,
			Interfaces:   []string{"eth0"},
		},
	}

	selector := NewInterfaceSelector(nil)
	component := NewBlackholeRouteComponent(cfg, selector)

	if component.GetRouteType() != RouteTypeBlackhole {
		t.Errorf("Expected route type %s, got %s", RouteTypeBlackhole, component.GetRouteType())
	}
}

// TestIPTablesRuleComponent_GetType tests that IPTables component returns correct type
func TestIPTablesRuleComponent_GetType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark: 0x100,
		},
		IPTablesRules: []*config.IPTablesRule{
			{
				Table: "mangle",
				Chain: "PREROUTING",
				Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "dst", "-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
		},
	}

	components, err := NewIPTablesRuleComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	if len(components) != 1 {
		t.Fatalf("Expected 1 component, got %d", len(components))
	}

	component := components[0]
	if component.GetType() != ComponentTypeIPTables {
		t.Errorf("Expected type %s, got %s", ComponentTypeIPTables, component.GetType())
	}
}

// TestIPTablesRuleComponent_ShouldExist tests that IPTables rules should always exist
func TestIPTablesRuleComponent_ShouldExist(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark: 0x100,
		},
		IPTablesRules: []*config.IPTablesRule{
			{
				Table: "mangle",
				Chain: "PREROUTING",
				Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "dst", "-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
		},
	}

	components, err := NewIPTablesRuleComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	component := components[0]
	if !component.ShouldExist() {
		t.Error("IPTables component should always return true for ShouldExist()")
	}
}

// TestIPTablesRuleComponent_GetRuleDescription tests rule description formatting
func TestIPTablesRuleComponent_GetRuleDescription(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark: 0x100,
		},
		IPTablesRules: []*config.IPTablesRule{
			{
				Table: "mangle",
				Chain: "PREROUTING",
				Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "dst", "-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
		},
	}

	components, err := NewIPTablesRuleComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	component := components[0]
	desc := component.GetRuleDescription()
	expectedDesc := "mangle/PREROUTING rule #1"
	if desc != expectedDesc {
		t.Errorf("Expected description '%s', got '%s'", expectedDesc, desc)
	}
}

// TestComponentBase_Embedding tests that all components properly embed ComponentBase
func TestComponentBase_Embedding(t *testing.T) {
	tests := []struct {
		name          string
		component     NetworkingComponent
		expectedType  ComponentType
		expectedIPSet string
	}{
		{
			name: "IPSet component",
			component: NewIPSetComponent(&config.IPSetConfig{
				IPSetName: "test-ipset",
				IPVersion: 4,
			}),
			expectedType:  ComponentTypeIPSet,
			expectedIPSet: "test-ipset",
		},
		{
			name: "IPRule component",
			component: NewIPRuleComponent(&config.IPSetConfig{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Routing: &config.RoutingConfig{
					FwMark:         0x100,
					IpRouteTable:   100,
					IpRulePriority: 100,
				},
			}),
			expectedType:  ComponentTypeIPRule,
			expectedIPSet: "test-ipset",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if tt.component.GetType() != tt.expectedType {
				t.Errorf("Expected type %s, got %s", tt.expectedType, tt.component.GetType())
			}
			if tt.component.GetIPSetName() != tt.expectedIPSet {
				t.Errorf("Expected IPSet name '%s', got '%s'", tt.expectedIPSet, tt.component.GetIPSetName())
			}
			if tt.component.GetDescription() == "" {
				t.Error("Expected non-empty description")
			}
		})
	}
}
