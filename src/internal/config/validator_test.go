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
					IpRouteTable:   100,
					IpRulePriority: 100,
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
		General: &GeneralConfig{},
	}
	
	err := config.validateIPSets()
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
					IpRouteTable:   100,
					IpRulePriority: 100,
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
					IpRouteTable:   100,
					IpRulePriority: 100,
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
					IpRouteTable:   100,
					IpRulePriority: 100,
				},
			},
			{
				IPSetName: "test",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth1"},
					FwMark:         101,
					IpRouteTable:   101,
					IpRulePriority: 101,
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
					IpRouteTable:   100,
					IpRulePriority: 100,
				},
			},
			{
				IPSetName: "test2",
				Lists:     []string{"test_list"},
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:     []string{"eth1"},
					FwMark:         101,
					IpRouteTable:   100,
					IpRulePriority: 101,
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

func TestValidateGeneralConfig_DefaultValues(t *testing.T) {
	config := &Config{
		General: &GeneralConfig{},
	}

	err := config.validateGeneralConfig()
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	if config.General.UseKeeneticDNS == nil || *config.General.UseKeeneticDNS {
		t.Error("Expected UseKeeneticDNS to default to false")
	}
}

func TestValidateIPSet_EmptyName(t *testing.T) {
	ipset := &IPSetConfig{
		IPSetName: "",
	}
	
	err := ipset.validateIPSet()
	if err == nil {
		t.Error("Expected error for empty ipset name")
	}
}

func TestValidateIPSet_InvalidName(t *testing.T) {
	ipset := &IPSetConfig{
		IPSetName: "Invalid-Name",
	}
	
	err := ipset.validateIPSet()
	if err == nil {
		t.Error("Expected error for invalid ipset name")
	}
}

func TestValidateIPSet_MissingRouting(t *testing.T) {
	ipset := &IPSetConfig{
		IPSetName: "test",
	}
	
	err := ipset.validateIPSet()
	if err == nil {
		t.Error("Expected error for missing routing config")
	}
}

func TestValidateIPSet_DefaultIPTablesRules(t *testing.T) {
	ipset := &IPSetConfig{
		IPSetName: "test",
		IPVersion: Ipv4,
		Routing: &RoutingConfig{
			Interfaces: []string{"eth0"},
		},
	}
	
	err := ipset.validateIPSet()
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	
	if len(ipset.IPTablesRules) != 1 {
		t.Error("Expected default iptables rule to be added")
	}
	
	rule := ipset.IPTablesRules[0]
	if rule.Chain != "PREROUTING" || rule.Table != "mangle" {
		t.Error("Expected default rule to be PREROUTING/mangle")
	}
}

func TestValidateIPTablesRules_MissingChain(t *testing.T) {
	ipset := &IPSetConfig{
		IPSetName: "test",
		IPVersion: Ipv4,
		Routing: &RoutingConfig{
			Interfaces: []string{"eth0"},
		},
		IPTablesRules: []*IPTablesRule{
			{
				Table: "mangle",
				Rule:  []string{"-j", "ACCEPT"},
			},
		},
	}
	
	err := ipset.validateIPSet()
	if err == nil {
		t.Error("Expected error for missing chain")
	}
}

func TestValidateIpVersion(t *testing.T) {
	tests := []struct {
		name        string
		version     IpFamily
		expectError bool
	}{
		{"IPv4 valid", Ipv4, false},
		{"IPv6 valid", Ipv6, false},
		{"Invalid version", IpFamily(99), true},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := validateIpVersion(tt.version)
			if tt.expectError && err == nil {
				t.Error("Expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("Expected no error but got: %v", err)
			}
		})
	}
}

func TestValidateNonEmpty(t *testing.T) {
	tests := []struct {
		name        string
		value       string
		expectError bool
	}{
		{"Non-empty string", "test", false},
		{"Empty string", "", true},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateNonEmpty(tt.value, "test_field")
			if tt.expectError && err == nil {
				t.Error("Expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("Expected no error but got: %v", err)
			}
		})
	}
}

func TestCheckIsDistinct(t *testing.T) {
	tests := []struct {
		name        string
		list        []string
		expectError bool
	}{
		{"Distinct values", []string{"a", "b", "c"}, false},
		{"Duplicate values", []string{"a", "b", "a"}, true},
		{"Empty list", []string{}, false},
		{"Single value", []string{"a"}, false},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := checkIsDistinct(tt.list, func(s string) string { return s })
			if tt.expectError && err == nil {
				t.Error("Expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("Expected no error but got: %v", err)
			}
		})
	}
}

func TestValidateDNSOverride(t *testing.T) {
	tests := []struct {
		name        string
		dnsOverride string
		expectError bool
	}{
		{"Empty string", "", false},
		{"Valid IPv4", "8.8.8.8", false},
		{"Valid IPv4 with port", "8.8.8.8#53", false},
		{"Valid IPv4 with high port", "1.1.1.1#8080", false},
		{"Valid IPv6", "2001:4860:4860::8888", false},
		{"Valid IPv6 with port", "2001:4860:4860::8888#53", false},
		{"Invalid IP", "invalid.ip", true},
		{"Invalid port - zero", "8.8.8.8#0", true},
		{"Invalid port - too high", "8.8.8.8#65536", true},
		{"Invalid port - non-numeric", "8.8.8.8#abc", true},
		{"Multiple # characters", "8.8.8.8#53#80", true},
		{"IP with # but no port", "8.8.8.8#", true},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateDNSOverride(tt.dnsOverride)
			if tt.expectError && err == nil {
				t.Error("Expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("Expected no error but got: %v", err)
			}
		})
	}
}

func TestValidateIPSet_DNSOverride(t *testing.T) {
	tests := []struct {
		name        string
		dnsOverride string
		expectError bool
	}{
		{"Valid DNS override", "8.8.8.8#53", false},
		{"No DNS override", "", false},
		{"Invalid DNS override", "invalid.ip", true},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ipset := &IPSetConfig{
				IPSetName: "test",
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					Interfaces:  []string{"eth0"},
					DNSOverride: tt.dnsOverride,
				},
			}
			
			err := ipset.validateIPSet()
			if tt.expectError && err == nil {
				t.Error("Expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("Expected no error but got: %v", err)
			}
		})
	}
}