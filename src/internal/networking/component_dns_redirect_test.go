package networking

import (
	"strings"
	"testing"
)

func TestDNSRedirectComponent_BasicMethods(t *testing.T) {
	// Create component with mock target port
	targetPort := uint16(5353)
	// We can't easily mock iptables here, so NewDNSRedirectComponent might fail if iptables is not present.
	// However, NewDNSRedirectComponent only fails if iptables.NewWithProtocol fails.
	// On many CI/dev envs, this might pass even without root, or fail.
	// If it fails, we skip.

	component, err := NewDNSRedirectComponent(targetPort)
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

	// Test GetCommand
	cmd := component.GetCommand()
	if !strings.Contains(cmd, "KEEN_PBR_DNS") {
		t.Errorf("Command should contain chain name, got: %s", cmd)
	}
}
