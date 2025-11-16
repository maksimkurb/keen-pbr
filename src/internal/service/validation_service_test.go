package service

import (
	"strings"
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

func TestValidationService_ValidateConfig(t *testing.T) {
	validator := NewValidationService()

	t.Run("Valid configuration", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Lists:     []string{"list1"},
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"lo"}, // loopback always exists
					},
				},
			},
			Lists: []*config.ListSource{
				{
					ListName: "list1",
					URL:      "http://example.com/list.txt",
				},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err != nil {
			t.Errorf("Expected valid config, got error: %v", err)
		}
	})

	t.Run("No ipsets", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for empty ipsets")
		}
		if !strings.Contains(err.Error(), "No ipsets defined") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("Duplicate ipset names", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{IPSetName: "test", IPVersion: config.Ipv4},
				{IPSetName: "test", IPVersion: config.Ipv4},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for duplicate ipset names")
		}
		if !strings.Contains(err.Error(), "Duplicate ipset name") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("Invalid IP version", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{IPSetName: "test", IPVersion: 5}, // Invalid
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for invalid IP version")
		}
		if !strings.Contains(err.Error(), "Invalid IP version") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("Undefined list reference", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Lists:     []string{"nonexistent"},
				},
			},
			Lists: []*config.ListSource{},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for undefined list")
		}
		if !strings.Contains(err.Error(), "undefined list") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("Routing table conflict", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test1",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"lo"},
					},
				},
				{
					IPSetName: "test2",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100, // Same table
						FwMark:         200,
						IpRulePriority: 200,
						Interfaces:     []string{"lo"},
					},
				},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for routing table conflict")
		}
		if !strings.Contains(err.Error(), "used by multiple ipsets") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("Invalid routing config - zero table", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   0, // Invalid
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"lo"},
					},
				},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for zero routing table")
		}
		if !strings.Contains(err.Error(), "Invalid routing table") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("No interfaces configured", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{}, // Empty
					},
				},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for no interfaces")
		}
		if !strings.Contains(err.Error(), "No interfaces defined") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("List with no source", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Lists:     []string{"list1"},
				},
			},
			Lists: []*config.ListSource{
				{
					ListName: "list1",
					// No URL, File, or Hosts
				},
			},
		}

		err := validator.ValidateConfig(cfg)
		if err == nil {
			t.Error("Expected error for list with no source")
		}
		if !strings.Contains(err.Error(), "has no source") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})
}

func TestValidationService_ValidateInterfaces(t *testing.T) {
	validator := NewValidationService()

	t.Run("At least one interface exists (loopback)", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"lo"}, // loopback always exists
					},
				},
			},
		}

		err := validator.validateInterfaces(cfg)
		if err != nil {
			t.Errorf("Expected no error for loopback interface, got: %v", err)
		}
	})

	t.Run("None of the configured interfaces exist", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"nonexistent1", "nonexistent2"},
					},
				},
			},
		}

		err := validator.validateInterfaces(cfg)
		if err == nil {
			t.Error("Expected error when no interfaces exist")
		}
		if !strings.Contains(err.Error(), "None of the configured interfaces") {
			t.Errorf("Unexpected error message: %v", err)
		}
	})

	t.Run("At least one exists among many", func(t *testing.T) {
		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IpRouteTable:   100,
						FwMark:         100,
						IpRulePriority: 100,
						Interfaces:     []string{"nonexistent1", "lo", "nonexistent2"},
					},
				},
			},
		}

		err := validator.validateInterfaces(cfg)
		if err != nil {
			t.Errorf("Expected no error when at least one interface exists, got: %v", err)
		}
	})
}
