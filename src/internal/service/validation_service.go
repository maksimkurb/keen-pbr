package service

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/errors"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// ValidationService provides centralized configuration validation.
//
// It validates various aspects of configuration including:
//   - IPSet configuration (names, routing tables, interfaces)
//   - Network interfaces existence
//   - List references
//   - Routing table conflicts
type ValidationService struct {
	// No dependencies needed - validation is pure logic
}

// NewValidationService creates a new validation service.
func NewValidationService() *ValidationService {
	return &ValidationService{}
}

// ValidateConfig performs comprehensive configuration validation.
//
// This runs all validators and returns the first error encountered.
func (v *ValidationService) ValidateConfig(cfg *config.Config) error {
	validators := []func(*config.Config) error{
		v.validateIPSets,
		v.validateLists,
		v.validateRoutingTables,
		v.validateInterfaces,
	}

	for _, validator := range validators {
		if err := validator(cfg); err != nil {
			return err
		}
	}

	return nil
}

// validateIPSets validates ipset configuration.
func (v *ValidationService) validateIPSets(cfg *config.Config) error {
	if len(cfg.IPSets) == 0 {
		return errors.NewConfigError("No ipsets defined in configuration", nil)
	}

	seenNames := make(map[string]bool)

	for _, ipset := range cfg.IPSets {
		// Check for duplicate names
		if seenNames[ipset.IPSetName] {
			return errors.NewConfigError(
				fmt.Sprintf("Duplicate ipset name: %s", ipset.IPSetName),
				nil,
			)
		}
		seenNames[ipset.IPSetName] = true

		// Validate ipset name is not empty
		if ipset.IPSetName == "" {
			return errors.NewConfigError("IPSet name cannot be empty", nil)
		}

		// Validate IP version
		if ipset.IPVersion != config.Ipv4 && ipset.IPVersion != config.Ipv6 {
			return errors.NewConfigError(
				fmt.Sprintf("Invalid IP version for ipset %s: %d (must be 4 or 6)",
					ipset.IPSetName, ipset.IPVersion),
				nil,
			)
		}

		// Validate routing configuration
		if ipset.Routing != nil {
			if err := v.validateRoutingConfig(ipset); err != nil {
				return err
			}
		}
	}

	return nil
}

// validateRoutingConfig validates routing configuration for an ipset.
func (v *ValidationService) validateRoutingConfig(ipset *config.IPSetConfig) error {
	if ipset.Routing.IpRouteTable <= 0 {
		return errors.NewConfigError(
			fmt.Sprintf("Invalid routing table for ipset %s: %d (must be > 0)",
				ipset.IPSetName, ipset.Routing.IpRouteTable),
			nil,
		)
	}

	if ipset.Routing.FwMark <= 0 {
		return errors.NewConfigError(
			fmt.Sprintf("Invalid fwmark for ipset %s: %d (must be > 0)",
				ipset.IPSetName, ipset.Routing.FwMark),
			nil,
		)
	}

	if ipset.Routing.IpRulePriority <= 0 {
		return errors.NewConfigError(
			fmt.Sprintf("Invalid ip rule priority for ipset %s: %d (must be > 0)",
				ipset.IPSetName, ipset.Routing.IpRulePriority),
			nil,
		)
	}

	// Interfaces are optional - if not specified, only blackhole route will be used

	return nil
}

// validateLists validates list references.
func (v *ValidationService) validateLists(cfg *config.Config) error {
	// Build map of defined lists
	definedLists := make(map[string]bool)
	for _, list := range cfg.Lists {
		definedLists[list.ListName] = true
	}

	// Check that all referenced lists are defined
	for _, ipset := range cfg.IPSets {
		for _, listName := range ipset.Lists {
			if !definedLists[listName] {
				return errors.NewConfigError(
					fmt.Sprintf("IPSet %s references undefined list: %s",
						ipset.IPSetName, listName),
					nil,
				)
			}
		}
	}

	// Validate list configurations
	for _, list := range cfg.Lists {
		if list.ListName == "" {
			return errors.NewConfigError("List name cannot be empty", nil)
		}

		if list.URL == "" && list.File == "" && len(list.Hosts) == 0 {
			return errors.NewConfigError(
				fmt.Sprintf("List %s has no source (URL, file, or hosts)", list.ListName),
				nil,
			)
		}
	}

	return nil
}

// validateRoutingTables checks for routing table conflicts.
func (v *ValidationService) validateRoutingTables(cfg *config.Config) error {
	seenTables := make(map[int]string) // table -> ipset name

	for _, ipset := range cfg.IPSets {
		if ipset.Routing == nil {
			continue
		}

		table := ipset.Routing.IpRouteTable

		if existingIPSet, exists := seenTables[table]; exists {
			return errors.NewConfigError(
				fmt.Sprintf("Routing table %d used by multiple ipsets: %s and %s",
					table, existingIPSet, ipset.IPSetName),
				nil,
			)
		}

		seenTables[table] = ipset.IPSetName
	}

	return nil
}

// validateInterfaces validates that all configured interfaces exist on the system.
//
// This performs a best-effort validation - if an interface doesn't exist now,
// it might exist later (e.g., VPN interfaces that come and go).
func (v *ValidationService) validateInterfaces(cfg *config.Config) error {
	// Collect all unique interface names
	interfaceNames := make(map[string]bool)

	for _, ipset := range cfg.IPSets {
		if ipset.Routing == nil {
			continue
		}

		for _, ifaceName := range ipset.Routing.Interfaces {
			interfaceNames[ifaceName] = true
		}
	}

	// Check each interface exists (at least one must exist per ipset)
	for _, ipset := range cfg.IPSets {
		if ipset.Routing == nil {
			continue
		}

		atLeastOneExists := false
		for _, ifaceName := range ipset.Routing.Interfaces {
			if _, err := networking.GetInterface(ifaceName); err == nil {
				atLeastOneExists = true
				break
			}
		}

		if !atLeastOneExists {
			return errors.NewConfigError(
				fmt.Sprintf("None of the configured interfaces for ipset %s exist on the system",
					ipset.IPSetName),
				nil,
			)
		}
	}

	return nil
}
