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

// TestComponentBuilder_BuildAllComponents tests building components for multiple IPSets
func TestComponentBuilder_BuildAllComponents(t *testing.T) {
	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "ipset1",
				IPVersion: 4,
				Routing: &config.RoutingConfig{
					FwMark:         0x100,
					IPRouteTable:   100,
					IPRulePriority: 100,
					Interfaces:     []string{"eth0"},
				},
				IPTablesRules: []*config.IPTablesRule{
					{
						Table: "mangle",
						Chain: "PREROUTING",
						Rule:  []string{"-j", "MARK", "--set-mark", "{{fwmark}}"},
					},
				},
			},
			{
				IPSetName: "ipset2",
				IPVersion: 4,
				Routing: &config.RoutingConfig{
					FwMark:         0x200,
					IPRouteTable:   200,
					IPRulePriority: 200,
					Interfaces:     []string{"eth1"},
				},
				IPTablesRules: []*config.IPTablesRule{
					{
						Table: "mangle",
						Chain: "PREROUTING",
						Rule:  []string{"-j", "MARK", "--set-mark", "{{fwmark}}"},
					},
				},
			},
		},
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildAllComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	// Should have components from both IPSets
	ipsetNames := make(map[string]bool)
	for _, component := range components {
		ipsetNames[component.GetIPSetName()] = true
	}

	if !ipsetNames["ipset1"] {
		t.Error("Expected components for ipset1")
	}
	if !ipsetNames["ipset2"] {
		t.Error("Expected components for ipset2")
	}

	// Should have at least 2 IPSet components (one per IPSet config)
	typeCounts := make(map[ComponentType]int)
	for _, component := range components {
		typeCounts[component.GetType()]++
	}
	if typeCounts[ComponentTypeIPSet] < 2 {
		t.Errorf("Expected at least 2 IPSet components, got %d", typeCounts[ComponentTypeIPSet])
	}
}

// TestGroupComponentsByIPSet tests grouping components by IPSet name
func TestGroupComponentsByIPSet(t *testing.T) {
	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "ipset1",
				IPVersion: 4,
				Routing: &config.RoutingConfig{
					FwMark:         0x100,
					IPRouteTable:   100,
					IPRulePriority: 100,
					Interfaces:     []string{"eth0"},
				},
				IPTablesRules: []*config.IPTablesRule{
					{
						Table: "mangle",
						Chain: "PREROUTING",
						Rule:  []string{"-j", "MARK", "--set-mark", "{{fwmark}}"},
					},
				},
			},
			{
				IPSetName: "ipset2",
				IPVersion: 4,
				Routing: &config.RoutingConfig{
					FwMark:         0x200,
					IPRouteTable:   200,
					IPRulePriority: 200,
					Interfaces:     []string{"eth1"},
				},
				IPTablesRules: []*config.IPTablesRule{
					{
						Table: "mangle",
						Chain: "PREROUTING",
						Rule:  []string{"-j", "MARK", "--set-mark", "{{fwmark}}"},
					},
				},
			},
		},
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildAllComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	grouped := GroupComponentsByIPSet(components)

	if len(grouped) < 2 {
		t.Errorf("Expected at least 2 groups, got %d", len(grouped))
	}

	if _, exists := grouped["ipset1"]; !exists {
		t.Error("Expected group for ipset1")
	}
	if _, exists := grouped["ipset2"]; !exists {
		t.Error("Expected group for ipset2")
	}

	// Each group should have multiple components
	for ipsetName, comps := range grouped {
		if len(comps) < 3 {
			t.Errorf("Expected at least 3 components for %s, got %d", ipsetName, len(comps))
		}
	}
}

// TestFilterComponentsByType tests filtering components by type
func TestFilterComponentsByType(t *testing.T) {
	cfg := &config.IPSetConfig{
		IPSetName: "test-ipset",
		IPVersion: 4,
		Routing: &config.RoutingConfig{
			FwMark:         0x100,
			IPRouteTable:   100,
			IPRulePriority: 100,
			Interfaces:     []string{"eth0"},
		},
		IPTablesRules: []*config.IPTablesRule{
			{
				Table: "mangle",
				Chain: "PREROUTING",
				Rule:  []string{"-j", "MARK", "--set-mark", "{{fwmark}}"},
			},
		},
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildComponents(cfg)
	if err != nil {
		t.Skipf("Skipping test - iptables not available: %v", err)
		return
	}

	tests := []struct {
		name        string
		filterType  ComponentType
		expectedMin int
		expectedMax int
	}{
		{
			name:        "Filter IPSet",
			filterType:  ComponentTypeIPSet,
			expectedMin: 1,
			expectedMax: 1,
		},
		{
			name:        "Filter IPRule",
			filterType:  ComponentTypeIPRule,
			expectedMin: 1,
			expectedMax: 1,
		},
		{
			name:        "Filter IPTables",
			filterType:  ComponentTypeIPTables,
			expectedMin: 1,
			expectedMax: 1,
		},
		{
			name:        "Filter IPRoute",
			filterType:  ComponentTypeIPRoute,
			expectedMin: 1,  // At least blackhole
			expectedMax: 10, // Could have multiple routes
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			filtered := FilterComponentsByType(components, tt.filterType)
			if len(filtered) < tt.expectedMin {
				t.Errorf("Expected at least %d components of type %s, got %d", tt.expectedMin, tt.filterType, len(filtered))
			}
			if len(filtered) > tt.expectedMax {
				t.Errorf("Expected at most %d components of type %s, got %d", tt.expectedMax, tt.filterType, len(filtered))
			}

			// Verify all filtered components are of the correct type
			for _, component := range filtered {
				if component.GetType() != tt.filterType {
					t.Errorf("Filtered component has wrong type: expected %s, got %s", tt.filterType, component.GetType())
				}
			}
		})
	}
}

// TestComponentBuilder_EmptyConfig tests handling of empty configuration
func TestComponentBuilder_EmptyConfig(t *testing.T) {
	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{},
	}

	builder := NewComponentBuilder(nil)
	components, err := builder.BuildAllComponents(cfg)
	if err != nil {
		t.Fatalf("BuildAllComponents failed: %v", err)
	}

	if len(components) != 0 {
		t.Errorf("Expected 0 components for empty config, got %d", len(components))
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
