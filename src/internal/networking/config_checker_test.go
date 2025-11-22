package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

func TestValidateInterfacesArePresent(t *testing.T) {
	tests := []struct {
		name        string
		config      *config.Config
		interfaces  []Interface
		expectError bool
		errorSubstr string
	}{
		{
			name: "All interfaces present",
			config: &config.Config{
				IPSets: []*config.IPSetConfig{
					{
						IPSetName: "test1",
						Routing: &config.RoutingConfig{
							Interfaces: []string{"eth0", "eth1"},
						},
					},
					{
						IPSetName: "test2", 
						Routing: &config.RoutingConfig{
							Interfaces: []string{"eth1", "wlan0"},
						},
					},
				},
			},
			interfaces: []Interface{
				{&mockNetlinkLink{name: "eth0"}},
				{&mockNetlinkLink{name: "eth1"}},
				{&mockNetlinkLink{name: "wlan0"}},
			},
			expectError: false,
		},
		{
			name: "Some interfaces missing but at least one valid",
			config: &config.Config{
				IPSets: []*config.IPSetConfig{
					{
						IPSetName: "test1",
						Routing: &config.RoutingConfig{
							Interfaces: []string{"eth0", "nonexistent"},
						},
					},
				},
			},
			interfaces: []Interface{
				{&mockNetlinkLink{name: "eth0"}},
				{&mockNetlinkLink{name: "eth1"}},
			},
			expectError: false, // Should not error since eth0 exists
		},
		{
			name: "Empty ipsets config",
			config: &config.Config{
				IPSets: []*config.IPSetConfig{},
			},
			interfaces: []Interface{
				{&mockNetlinkLink{name: "eth0"}},
			},
			expectError: false,
		},
		{
			name: "All interfaces missing",
			config: &config.Config{
				IPSets: []*config.IPSetConfig{
					{
						IPSetName: "test1",
						Routing: &config.RoutingConfig{
							Interfaces: []string{"eth0", "nonexistent"},
						},
					},
				},
			},
			interfaces:  []Interface{},
			expectError: true,
			errorSubstr: "test1",
		},
		{
			name: "Multiple IPSets with mixed interface availability",
			config: &config.Config{
				IPSets: []*config.IPSetConfig{
					{
						IPSetName: "test1",
						Routing: &config.RoutingConfig{
							Interfaces: []string{"eth0", "missing1"}, // eth0 exists
						},
					},
					{
						IPSetName: "test2",
						Routing: &config.RoutingConfig{
							Interfaces: []string{"missing2", "missing3"}, // all missing
						},
					},
				},
			},
			interfaces: []Interface{
				{&mockNetlinkLink{name: "eth0"}},
				{&mockNetlinkLink{name: "eth1"}},
			},
			expectError: true,
			errorSubstr: "test2", // Should fail because test2 has no valid interfaces
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateInterfacesArePresent(tt.config, tt.interfaces)

			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				} else if tt.errorSubstr != "" && !contains(err.Error(), tt.errorSubstr) {
					t.Errorf("Expected error to contain '%s', got: %v", tt.errorSubstr, err)
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
			}
		})
	}
}

func TestValidateInterfaceExists(t *testing.T) {
	interfaces := []Interface{
		{&mockNetlinkLink{name: "eth0"}},
		{&mockNetlinkLink{name: "eth1"}},
		{&mockNetlinkLink{name: "wlan0"}},
	}

	tests := []struct {
		name          string
		interfaceName string
		expectError   bool
	}{
		{
			name:          "Interface exists",
			interfaceName: "eth1",
			expectError:   false,
		},
		{
			name:          "Interface does not exist",
			interfaceName: "nonexistent",
			expectError:   true,
		},
		{
			name:          "Empty interface name",
			interfaceName: "",
			expectError:   true,
		},
		{
			name:          "Case sensitive check",
			interfaceName: "ETH0",
			expectError:   true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateInterfaceExists(tt.interfaceName, interfaces)

			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
			}
		})
	}
}

func TestValidateInterfaceExists_EmptyList(t *testing.T) {
	err := validateInterfaceExists("eth0", []Interface{})
	if err == nil {
		t.Error("Expected error for empty interfaces list")
	}
}

