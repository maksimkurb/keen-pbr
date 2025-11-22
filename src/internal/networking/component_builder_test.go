package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// TestComponentBuilder_BuildComponents tests that builder creates all expected components
func TestComponentBuilder_BuildComponents(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IPRouteTable:   100,
			IPRulePriority: 100,
			Interfaces:     []string{"eth0", "eth1"},
		},
		IPTablesRules: []*config.IPTablesRule{
			{
				Table: "mangle",
				Chain: "PREROUTING",
				Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "dst", "-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
			{
				Table: "mangle",
				Chain: "OUTPUT",
				Rule:  []string{"-m", "set", "--match-set", "{{ipset_name}}", "dst", "-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
		},
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	// Count components by type
	typeCounts := make(map[ComponentType]int)
	for _, component := range components {
		typeCounts[component.GetType()]++
	}

	// Expected: 1 IPSet + 1 IPRule + 2 IPTables + routes (2 default + 1 blackhole, but may fail if interfaces don't exist)
	if typeCounts[ComponentTypeIPSet] != 1 {
		t.Errorf("Expected 1 IPSet component, got %d", typeCounts[ComponentTypeIPSet])
	}
	if typeCounts[ComponentTypeIPRule] != 1 {
		t.Errorf("Expected 1 IPRule component, got %d", typeCounts[ComponentTypeIPRule])
	}
	if typeCounts[ComponentTypeIPTables] != 2 {
		t.Errorf("Expected 2 IPTables components, got %d", typeCounts[ComponentTypeIPTables])
	}
	// Route count varies based on interface availability, but at minimum we should have blackhole
	if typeCounts[ComponentTypeIPRoute] < 1 {
		t.Errorf("Expected at least 1 IPRoute component (blackhole), got %d", typeCounts[ComponentTypeIPRoute])
	}
}

// TestComponentBuilder_BuildComponentsNoIPTables tests builder without IPTables rules
func TestComponentBuilder_BuildComponentsNoIPTables(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IPRouteTable:   100,
			IPRulePriority: 100,
			Interfaces:     []string{"eth0", "eth1"},
		},
		IPTablesRules: nil, // No IPTables rules
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildComponents(cfg)
	if err != nil {
		t.Fatalf("BuildComponents failed: %v", err)
	}

	// Count components by type
	typeCounts := make(map[ComponentType]int)
	for _, component := range components {
		typeCounts[component.GetType()]++
	}

	// Expected: 1 IPSet + 1 IPRule + 0 IPTables + routes
	if typeCounts[ComponentTypeIPSet] != 1 {
		t.Errorf("Expected 1 IPSet component, got %d", typeCounts[ComponentTypeIPSet])
	}
	if typeCounts[ComponentTypeIPRule] != 1 {
		t.Errorf("Expected 1 IPRule component, got %d", typeCounts[ComponentTypeIPRule])
	}
	if typeCounts[ComponentTypeIPTables] != 0 {
		t.Errorf("Expected 0 IPTables components, got %d", typeCounts[ComponentTypeIPTables])
	}
	// Route count varies based on interface availability, but at minimum we should have blackhole
	if typeCounts[ComponentTypeIPRoute] < 1 {
		t.Errorf("Expected at least 1 IPRoute component (blackhole), got %d", typeCounts[ComponentTypeIPRoute])
	}
}

// TestComponentBuilder_MinimalConfig tests handling of minimal valid configuration
func TestComponentBuilder_MinimalConfig(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "minimal-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IPRouteTable:   100,
			IPRulePriority: 100,
			Interfaces:     []string{},
		},
		IPTablesRules: nil,
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildComponents(cfg)
	if err != nil {
		t.Fatalf("BuildComponents failed: %v", err)
	}

	// Should have at least IPSet, IPRule, and blackhole route
	typeCounts := make(map[ComponentType]int)
	for _, component := range components {
		typeCounts[component.GetType()]++
	}

	if typeCounts[ComponentTypeIPSet] != 1 {
		t.Errorf("Expected 1 IPSet component, got %d", typeCounts[ComponentTypeIPSet])
	}
	if typeCounts[ComponentTypeIPRule] != 1 {
		t.Errorf("Expected 1 IPRule component, got %d", typeCounts[ComponentTypeIPRule])
	}
	if typeCounts[ComponentTypeIPRoute] < 1 {
		t.Errorf("Expected at least 1 IPRoute component (blackhole), got %d", typeCounts[ComponentTypeIPRoute])
	}
	if typeCounts[ComponentTypeIPTables] != 0 {
		t.Errorf("Expected 0 IPTables components for nil rules, got %d", typeCounts[ComponentTypeIPTables])
	}
}
