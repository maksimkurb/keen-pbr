package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// TestInterfaceSelector_WithMockKeenetic demonstrates using mocks for testing
func TestInterfaceSelector_WithMockKeenetic(t *testing.T) {
	// This test demonstrates that mocks are only imported in test files,
	// keeping them out of the production binary

	t.Run("No Keenetic client - falls back to system interfaces only", func(t *testing.T) {
		selector := NewInterfaceSelector(nil)

		// Create a test ipset config
		ipsetConfig := &config.IPSetConfig{
			IPSetName: "test-ipset",
			Routing: &config.RoutingConfig{
				Interfaces: []string{"lo"}, // loopback should always exist
			},
		}

		// This should work without Keenetic API
		iface, idx, err := selector.ChooseBest(ipsetConfig)
		if err != nil {
			t.Fatalf("Expected no error with nil client, got: %v", err)
		}

		// Loopback should be found (may or may not be chosen depending on state)
		_ = iface
		_ = idx
	})

	// Note: Tests with MockKeeneticClient are not included here to avoid import cycles.
	// Mock usage examples for Keenetic client can be found in src/internal/mocks/keenetic_test.go
	// Once the networking package is refactored to use domain interfaces instead of concrete
	// Client types, we can add full mock integration tests here.
}

// TestInterfaceSelector_IsUsable tests the interface usability check
func TestInterfaceSelector_IsUsable(t *testing.T) {
	selector := NewInterfaceSelector(nil)

	// Use loopback interface for testing (should always exist and be up)
	testIface, err := GetInterface("lo")
	if err != nil {
		t.Skipf("Skipping test: loopback interface not available: %v", err)
	}

	t.Run("Interface is usable when up and connected", func(t *testing.T) {
		keeneticIface := &keenetic.Interface{
			ID:        "Wireguard0",
			Connected: keenetic.KEENETIC_CONNECTED,
			Link:      keenetic.KEENETIC_LINK_UP,
		}

		isUsable := selector.IsUsable(testIface, keeneticIface)
		if !isUsable {
			t.Error("Expected interface to be usable when up and connected")
		}
	})

	t.Run("Interface is not usable when disconnected", func(t *testing.T) {
		keeneticIface := &keenetic.Interface{
			ID:        "Wireguard0",
			Connected: "no",
			Link:      "down",
		}

		isUsable := selector.IsUsable(testIface, keeneticIface)
		if isUsable {
			t.Error("Expected interface to not be usable when disconnected")
		}
	})

	t.Run("Interface is usable when connected field is empty (unknown status)", func(t *testing.T) {
		keeneticIface := &keenetic.Interface{
			ID:        "GigabitEthernet1",
			Connected: "", // Empty string - Keenetic API returned incomplete data
			Link:      keenetic.KEENETIC_LINK_UP,
		}

		isUsable := selector.IsUsable(testIface, keeneticIface)
		if !isUsable {
			t.Error("Expected interface to be usable when connected field is empty (should trust system status)")
		}
	})
}

// Note: More comprehensive tests will be added when we refactor Client to accept
// the domain.KeeneticClient interface instead of *keenetic.Client.
// This is tracked in the refactoring plan and will allow full mock usage.
