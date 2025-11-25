package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestValidateConfig_Success(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	err := os.WriteFile(testFile, []byte("test"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}

	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp",
		},
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test_ipset",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         100,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "test_list",
				File:     testFile,
			},
		},
		_absConfigFilePath: tmpDir,
	}

	err = config.ValidateConfig()
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
}

func TestValidateConfig_MissingGeneral(t *testing.T) {
	config := &Config{}

	err := config.ValidateConfig()
	if err == nil {
		t.Error("Expected error for missing general config")
	}
}

func TestValidateIPSets_MissingIPSets(t *testing.T) {
	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp",
		},
	}

	err := config.ValidateConfig()
	if err == nil {
		t.Error("Expected error for missing ipsets")
	}
}

func TestValidateIPSets_InvalidIPSetName(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "Invalid-Name",
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces: []string{"eth0"},
				},
			},
		},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for invalid ipset name")
	}
}

func TestValidateIPSets_MissingInterfaces(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces: []string{},
				},
			},
		},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for missing interfaces")
	}
}

func TestValidateIPSets_DuplicateInterfaces(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0", "eth0"},
					FwMark:         100,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "test_list",
				Hosts:    []string{"example.com"},
			},
		},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for duplicate interfaces")
	}
}

func TestValidateIPSets_UnknownList(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				Lists:     []string{"unknown_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         100,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
		Lists: []*ListSource{},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for unknown list")
	}
}

func TestValidateIPSets_DuplicateNames(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         100,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
			{
				IPSetName: "test",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth1"},
					FwMark:         101,
					IPRouteTable:   101,
					IPRulePriority: 101,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "test_list",
				Hosts:    []string{"example.com"},
			},
		},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for duplicate ipset names")
	}
}

func TestValidateIPSets_DuplicateRoutingTable(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test1",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth0"},
					FwMark:         100,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
			{
				IPSetName: "test2",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth1"},
					FwMark:         101,
					IPRouteTable:   100,
					IPRulePriority: 101,
				},
			},
		},
		Lists: []*ListSource{
			{
				ListName: "test_list",
				Hosts:    []string{"example.com"},
			},
		},
	}

	err := config.validateIPSets()
	if err == nil {
		t.Error("Expected error for duplicate routing table")
	}
}

func TestValidateLists_MissingListName(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				URL: "http://example.com",
			},
		},
	}

	err := config.validateLists()
	if err == nil {
		t.Error("Expected error for missing list name")
	}
}

func TestValidateLists_NoSource(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				ListName: "test",
			},
		},
	}

	err := config.validateLists()
	if err == nil {
		t.Error("Expected error for missing source (url/file/hosts)")
	}
}

func TestValidateLists_MultipleSources(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				ListName: "test",
				URL:      "http://example.com",
				Hosts:    []string{"example.com"},
			},
		},
	}

	err := config.validateLists()
	if err == nil {
		t.Error("Expected error for multiple sources")
	}
}

func TestValidateLists_NonExistentFile(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				ListName: "test",
				File:     "/non/existent/file.txt",
			},
		},
		_absConfigFilePath: "/tmp",
	}

	err := config.validateLists()
	if err == nil {
		t.Error("Expected error for non-existent file")
	}
}

func TestValidateLists_DuplicateNames(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				ListName: "test",
				Hosts:    []string{"example.com"},
			},
			{
				ListName: "test",
				URL:      "http://example.com",
			},
		},
	}

	err := config.validateLists()
	if err == nil {
		t.Error("Expected error for duplicate list names")
	}
}
