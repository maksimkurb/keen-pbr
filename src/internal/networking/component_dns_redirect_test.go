package networking

import (
	"strings"
	"testing"
)

func TestDNSRedirectComponent_BasicMethods(t *testing.T) {
	// Create component with mock target IPs and port
	targetIPv4 := "127.0.53.53"
	targetIPv6 := "::1"
	targetPort := uint16(5353)
	// We can't easily mock iptables here, so NewDNSRedirectComponent might fail if iptables is not present.
	// However, NewDNSRedirectComponent only fails if iptables.NewWithProtocol fails.
	// On many CI/dev envs, this might pass even without root, or fail.
	// If it fails, we skip.

	component, err := NewDNSRedirectComponent(targetIPv4, targetIPv6, targetPort)
	if err != nil {
		t.Skipf("Skipping test - failed to create component (likely no iptables): %v", err)
		return
	}

	// Test GetType
	if component.GetType() != ComponentTypeIPTables {
		t.Errorf("Expected type %s, got %s", ComponentTypeIPTables, component.GetType())
	}

	// Test GetIPSetName
	if component.GetIPSetName() != "" {
		t.Errorf("Expected empty IPSet name, got '%s'", component.GetIPSetName())
	}

	// Test GetDescription
	desc := component.GetDescription()
	if !strings.Contains(desc, "5353") {
		t.Errorf("Description should contain target port 5353, got: %s", desc)
	}

	// Test ShouldExist
	if !component.ShouldExist() {
		t.Error("ShouldExist should return true")
	}
}
