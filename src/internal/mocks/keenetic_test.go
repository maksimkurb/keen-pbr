package mocks

import (
	"errors"
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// TestMockKeeneticClient_DefaultBehavior verifies that the mock returns sensible defaults
func TestMockKeeneticClient_DefaultBehavior(t *testing.T) {
	mock := NewMockKeeneticClient()

	// Test default version
	version, err := mock.GetVersion()
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
	if version.Major != 4 || version.Minor != 3 {
		t.Errorf("Expected version 4.3, got %d.%d", version.Major, version.Minor)
	}

	// Test default interfaces
	interfaces, err := mock.GetInterfaces()
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
	if len(interfaces) != 1 {
		t.Errorf("Expected 1 default interface, got %d", len(interfaces))
	}
	if iface, ok := interfaces["vpn0"]; !ok {
		t.Error("Expected vpn0 interface in defaults")
	} else {
		if iface.Connected != keenetic.KeeneticConnected {
			t.Error("Expected default interface to be connected")
		}
	}

	// Test default DNS servers
	servers, err := mock.GetDNSServers()
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
	if len(servers) != 0 {
		t.Errorf("Expected empty DNS servers list, got %d", len(servers))
	}
}

// TestMockKeeneticClient_CustomVersion tests custom version behavior
func TestMockKeeneticClient_CustomVersion(t *testing.T) {
	mock := NewMockKeeneticClientWithVersion(3, 9)

	version, err := mock.GetVersion()
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
	if version.Major != 3 || version.Minor != 9 {
		t.Errorf("Expected version 3.9, got %d.%d", version.Major, version.Minor)
	}
}

// TestMockKeeneticClient_CustomInterfaces tests custom interfaces behavior
func TestMockKeeneticClient_CustomInterfaces(t *testing.T) {
	customInterfaces := map[string]keenetic.Interface{
		"eth0": {
			ID:         "Bridge0",
			Type:       "Bridge",
			Connected:  keenetic.KeeneticConnected,
			SystemName: "eth0",
		},
		"wlan0": {
			ID:         "WifiMaster0",
			Type:       "Wifi",
			Connected:  "no", // Not connected
			Link:       "down",
			SystemName: "wlan0",
		},
	}

	mock := NewMockKeeneticClientWithInterfaces(customInterfaces)

	interfaces, err := mock.GetInterfaces()
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
	if len(interfaces) != 2 {
		t.Errorf("Expected 2 interfaces, got %d", len(interfaces))
	}

	eth0, ok := interfaces["eth0"]
	if !ok {
		t.Fatal("Expected eth0 interface")
	}
	if eth0.Connected != keenetic.KeeneticConnected {
		t.Error("Expected eth0 to be connected")
	}

	wlan0, ok := interfaces["wlan0"]
	if !ok {
		t.Fatal("Expected wlan0 interface")
	}
	if wlan0.Connected != "no" {
		t.Error("Expected wlan0 to be disconnected")
	}
	if wlan0.Link != "down" {
		t.Error("Expected wlan0 link to be down")
	}
}

// TestMockKeeneticClient_CustomFunctions tests custom function behavior
func TestMockKeeneticClient_CustomFunctions(t *testing.T) {
	expectedErr := errors.New("test error")

	mock := &MockKeeneticClient{
		GetVersionFunc: func() (*keenetic.KeeneticVersion, error) {
			return nil, expectedErr
		},
		GetInterfacesFunc: func() (map[string]keenetic.Interface, error) {
			return nil, expectedErr
		},
		GetDNSServersFunc: func() ([]keenetic.DNSServerInfo, error) {
			return nil, expectedErr
		},
	}

	// Test custom GetVersion
	_, err := mock.GetVersion()
	if err != expectedErr {
		t.Errorf("Expected custom error, got: %v", err)
	}

	// Test custom GetInterfaces
	_, err = mock.GetInterfaces()
	if err != expectedErr {
		t.Errorf("Expected custom error, got: %v", err)
	}

	// Test custom GetDNSServers
	_, err = mock.GetDNSServers()
	if err != expectedErr {
		t.Errorf("Expected custom error, got: %v", err)
	}
}

// TestMockKeeneticClient_InterfaceStateScenarios tests various interface state scenarios
func TestMockKeeneticClient_InterfaceStateScenarios(t *testing.T) {
	scenarios := []struct {
		name       string
		interfaces map[string]keenetic.Interface
		expectFunc func(t *testing.T, interfaces map[string]keenetic.Interface)
	}{
		{
			name: "All interfaces down",
			interfaces: map[string]keenetic.Interface{
				"vpn0": {
					ID:         "Wireguard0",
					Connected:  "no",
					Link:       "down",
					SystemName: "vpn0",
				},
			},
			expectFunc: func(t *testing.T, interfaces map[string]keenetic.Interface) {
				if interfaces["vpn0"].Connected != "no" {
					t.Error("Expected vpn0 to be disconnected")
				}
			},
		},
		{
			name: "Multiple VPN interfaces, one connected",
			interfaces: map[string]keenetic.Interface{
				"vpn0": {
					ID:         "Wireguard0",
					Connected:  "no",
					Link:       "down",
					SystemName: "vpn0",
				},
				"vpn1": {
					ID:         "Wireguard1",
					Connected:  keenetic.KeeneticConnected,
					Link:       keenetic.KeeneticLinkUp,
					SystemName: "vpn1",
				},
			},
			expectFunc: func(t *testing.T, interfaces map[string]keenetic.Interface) {
				if interfaces["vpn0"].Connected != "no" {
					t.Error("Expected vpn0 to be disconnected")
				}
				if interfaces["vpn1"].Connected != keenetic.KeeneticConnected {
					t.Error("Expected vpn1 to be connected")
				}
			},
		},
		{
			name:       "No interfaces available",
			interfaces: map[string]keenetic.Interface{},
			expectFunc: func(t *testing.T, interfaces map[string]keenetic.Interface) {
				if len(interfaces) != 0 {
					t.Errorf("Expected 0 interfaces, got %d", len(interfaces))
				}
			},
		},
	}

	for _, scenario := range scenarios {
		t.Run(scenario.name, func(t *testing.T) {
			mock := NewMockKeeneticClientWithInterfaces(scenario.interfaces)
			interfaces, err := mock.GetInterfaces()
			if err != nil {
				t.Fatalf("Expected no error, got: %v", err)
			}
			scenario.expectFunc(t, interfaces)
		})
	}
}
