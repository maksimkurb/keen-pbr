package config

import (
	"os"
	"testing"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// mockKeeneticClient is a simple mock that returns empty DNS servers
type mockKeeneticClient struct{}

func (m *mockKeeneticClient) GetDNSServers() ([]keenetic.DnsServerInfo, error) {
	return []keenetic.DnsServerInfo{}, nil
}

// TestConfigHasher_NoDeadlockWithKeeneticClient tests that calculating hash
// with Keenetic client set doesn't cause a deadlock
func TestConfigHasher_NoDeadlockWithKeeneticClient(t *testing.T) {
	// Create a temporary config file
	configPath := "/tmp/test-config-deadlock.toml"
	configContent := `[general]
lists_output_dir = "/tmp/lists"
use_keenetic_dns = true
fallback_dns = "1.1.1.1"

[[lists]]
list_name = "test"
hosts = ["1.1.1.1"]

[[ipsets]]
ipset_name = "test_set"
lists = ["test"]
ip_version = 4

[ipsets.routing]
interfaces = ["eth0"]
ip_route_table = 100
ip_rule_priority = 100
fwmark = 100
`

	// Write config to file
	if err := os.WriteFile(configPath, []byte(configContent), 0644); err != nil {
		t.Fatalf("Failed to write config: %v", err)
	}
	defer os.Remove(configPath)

	// Create hasher with Keenetic client
	hasher := NewConfigHasher(configPath)
	mockClient := &mockKeeneticClient{}
	hasher.SetKeeneticClient(mockClient)

	// This should complete within 1 second if there's no deadlock
	done := make(chan bool, 1)
	errChan := make(chan error, 1)

	go func() {
		_, err := hasher.UpdateCurrentConfigHash()
		if err != nil {
			errChan <- err
			return
		}
		done <- true
	}()

	select {
	case err := <-errChan:
		t.Fatalf("Failed to update hash: %v", err)
	case <-done:
		// Success - no deadlock
		t.Log("Hash calculation completed successfully")
	case <-time.After(2 * time.Second):
		t.Fatal("DEADLOCK DETECTED: UpdateCurrentConfigHash() did not complete within 2 seconds")
	}
}

func boolPtr(b bool) *bool {
	return &b
}
