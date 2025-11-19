package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestConfigHasher_DeterministicHash(t *testing.T) {
	cfg := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
			FallbackDNS:    "8.8.8.8",
			APIBindAddress: "0.0.0.0:8080",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         0x100,
					IpRouteTable:   100,
					IpRulePriority: 100,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com", "test.com"},
			},
		},
	}

	hasher1 := NewConfigHasher(cfg)
	hash1, err := hasher1.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash: %v", err)
	}

	hasher2 := NewConfigHasher(cfg)
	hash2, err := hasher2.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash: %v", err)
	}

	if hash1 != hash2 {
		t.Errorf("Hashes should be identical, got %s and %s", hash1, hash2)
	}

	if hash1 == "" {
		t.Error("Hash should not be empty")
	}
}

func TestConfigHasher_UnusedListsIgnored(t *testing.T) {
	cfg1 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com"},
			},
		},
	}

	cfg2 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com"},
			},
			{
				ListName: "list2-unused",
				Hosts:    []string{"unused.com"},
			},
		},
	}

	hasher1 := NewConfigHasher(cfg1)
	hash1, err := hasher1.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 1: %v", err)
	}

	hasher2 := NewConfigHasher(cfg2)
	hash2, err := hasher2.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 2: %v", err)
	}

	if hash1 != hash2 {
		t.Errorf("Hashes should be identical (unused list should be ignored), got %s and %s", hash1, hash2)
	}
}

func TestConfigHasher_UsedListsIncluded(t *testing.T) {
	cfg1 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com"},
			},
		},
	}

	cfg2 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com", "newhost.com"},
			},
		},
	}

	hasher1 := NewConfigHasher(cfg1)
	hash1, err := hasher1.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 1: %v", err)
	}

	hasher2 := NewConfigHasher(cfg2)
	hash2, err := hasher2.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 2: %v", err)
	}

	if hash1 == hash2 {
		t.Errorf("Hashes should differ when used list content changes, got %s", hash1)
	}
}

func TestConfigHasher_OrderIndependent(t *testing.T) {
	cfg1 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1", "list2"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"a.com", "b.com"},
			},
			{
				ListName: "list2",
				Hosts:    []string{"c.com", "d.com"},
			},
		},
	}

	cfg2 := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list2", "list1"}, // Different order
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"b.com", "a.com"}, // Different order
			},
			{
				ListName: "list2",
				Hosts:    []string{"d.com", "c.com"}, // Different order
			},
		},
	}

	hasher1 := NewConfigHasher(cfg1)
	hash1, err := hasher1.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 1: %v", err)
	}

	hasher2 := NewConfigHasher(cfg2)
	hash2, err := hasher2.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 2: %v", err)
	}

	if hash1 != hash2 {
		t.Errorf("Hashes should be identical (order independent), got %s and %s", hash1, hash2)
	}
}

func TestConfigHasher_AllSettingsIncluded(t *testing.T) {
	baseConfig := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp/lists",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"list1"},
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         0x100,
					IpRouteTable:   100,
					IpRulePriority: 100,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "list1",
				Hosts:    []string{"example.com"},
			},
		},
	}

	// Get base hash
	hasher := NewConfigHasher(baseConfig)
	baseHash, err := hasher.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate base hash: %v", err)
	}

	// Test changing general settings
	t.Run("General settings change", func(t *testing.T) {
		modifiedConfig := &Config{
			General: &GeneralConfig{
				ListsOutputDir: "/tmp/different",
			},
			IPSets: baseConfig.IPSets,
			Lists:  baseConfig.Lists,
		}
		hasher := NewConfigHasher(modifiedConfig)
		hash, _ := hasher.CalculateHash()
		if hash == baseHash {
			t.Error("Hash should change when general settings change")
		}
	})

	// Test changing routing settings
	t.Run("Routing settings change", func(t *testing.T) {
		modifiedConfig := &Config{
			General: baseConfig.General,
			IPSets: []*IPSetConfig{
				{
					IPSetName: "test-ipset",
					IPVersion: 4,
					Lists:     []string{"list1"},
					Routing: &RoutingConfig{
						Interfaces:     []string{"eth1"}, // Changed
						FwMark:         0x100,
						IpRouteTable:   100,
						IpRulePriority: 100,
					},
				},
			},
			Lists: baseConfig.Lists,
		}
		hasher := NewConfigHasher(modifiedConfig)
		hash, _ := hasher.CalculateHash()
		if hash == baseHash {
			t.Error("Hash should change when routing interface changes")
		}
	})

	// Test changing fwmark
	t.Run("FwMark change", func(t *testing.T) {
		modifiedConfig := &Config{
			General: baseConfig.General,
			IPSets: []*IPSetConfig{
				{
					IPSetName: "test-ipset",
					IPVersion: 4,
					Lists:     []string{"list1"},
					Routing: &RoutingConfig{
						Interfaces:     []string{"eth0"},
						FwMark:         0x200, // Changed
						IpRouteTable:   100,
						IpRulePriority: 100,
					},
				},
			},
			Lists: baseConfig.Lists,
		}
		hasher := NewConfigHasher(modifiedConfig)
		hash, _ := hasher.CalculateHash()
		if hash == baseHash {
			t.Error("Hash should change when fwmark changes")
		}
	})
}

func TestConfigHasher_FileListHash(t *testing.T) {
	// Create temp directory and file
	tmpDir := t.TempDir()
	listFile := filepath.Join(tmpDir, "test-list.txt")

	err := os.WriteFile(listFile, []byte("example.com\ntest.com\n"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}

	cfg := &Config{
		General: &GeneralConfig{
			ListsOutputDir: tmpDir,
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: 4,
				Lists:     []string{"test-list"},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "test-list",
				File:     listFile,
			},
		},
		_absConfigFilePath: tmpDir,
	}

	hasher := NewConfigHasher(cfg)
	hash1, err := hasher.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash: %v", err)
	}

	// Hash should be consistent
	hash2, err := hasher.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 2: %v", err)
	}

	if hash1 != hash2 {
		t.Errorf("File-based list hashes should be consistent, got %s and %s", hash1, hash2)
	}

	// Modify file
	err = os.WriteFile(listFile, []byte("example.com\ntest.com\nnewhost.com\n"), 0644)
	if err != nil {
		t.Fatalf("Failed to modify test file: %v", err)
	}

	hash3, err := hasher.CalculateHash()
	if err != nil {
		t.Fatalf("Failed to calculate hash 3: %v", err)
	}

	if hash1 == hash3 {
		t.Errorf("Hash should change when file content changes, got %s", hash1)
	}
}
