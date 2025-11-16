// Package service provides business logic orchestration for keen-pbr.
//
// The service layer sits between commands (CLI controllers) and domain logic,
// coordinating operations across multiple managers while keeping commands simple.
package service

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ApplyOptions configures which operations to perform during Apply.
type ApplyOptions struct {
	SkipIPSet   bool
	SkipRouting bool
	OnlyInterface *string // If set, only apply routing for this interface
}

// RoutingService orchestrates routing and network configuration operations.
//
// It coordinates between network managers, ipset managers, and validation,
// providing a high-level API for applying, updating, and removing configurations.
type RoutingService struct {
	networkManager domain.NetworkManager
	ipsetManager   domain.IPSetManager
	validator      *ValidationService
}

// NewRoutingService creates a new routing service.
//
// Parameters:
//   - networkManager: Handles iptables, ip rules, and ip routes
//   - ipsetManager: Handles ipset creation and population
//   - validator: Validates configuration before application (optional, can be nil)
func NewRoutingService(
	networkManager domain.NetworkManager,
	ipsetManager domain.IPSetManager,
	validator *ValidationService,
) *RoutingService {
	return &RoutingService{
		networkManager: networkManager,
		ipsetManager:   ipsetManager,
		validator:      validator,
	}
}

// Apply applies the complete routing configuration.
//
// This orchestrates the full workflow:
//  1. Validates configuration (if validator provided)
//  2. Creates ipsets (unless SkipIPSet is true)
//  3. Applies persistent network config (unless SkipRouting is true)
//  4. Applies dynamic routing config (unless SkipRouting is true)
//
// Returns error if any step fails.
func (s *RoutingService) Apply(cfg *config.Config, opts ApplyOptions) error {
	// Validate configuration
	if s.validator != nil {
		if err := s.validator.ValidateConfig(cfg); err != nil {
			log.Errorf("Configuration validation failed: %v", err)
			return err
		}
	}

	// Create ipsets
	if !opts.SkipIPSet {
		log.Infof("Creating ipsets...")
		if err := s.ipsetManager.CreateIfAbsent(cfg); err != nil {
			log.Errorf("Failed to create ipsets: %v", err)
			return err
		}
	}

	// Apply network configuration
	if !opts.SkipRouting {
		// Filter ipsets if only specific interface requested
		ipsets := cfg.IPSets
		if opts.OnlyInterface != nil {
			ipsets = s.filterIPSetsByInterface(cfg.IPSets, *opts.OnlyInterface)
			if len(ipsets) == 0 {
				log.Warnf("No ipsets found for interface %s", *opts.OnlyInterface)
				return nil
			}
		}

		// Apply persistent configuration (iptables rules, ip rules)
		log.Infof("Applying persistent network configuration...")
		if err := s.networkManager.ApplyPersistentConfig(ipsets); err != nil {
			log.Errorf("Failed to apply persistent config: %v", err)
			return err
		}

		// Apply routing configuration (ip routes)
		log.Infof("Applying routing configuration...")
		if err := s.networkManager.ApplyRoutingConfig(ipsets); err != nil {
			log.Errorf("Failed to apply routing config: %v", err)
			return err
		}
	}

	log.Infof("Configuration applied successfully")
	return nil
}

// ApplyPersistentOnly applies only persistent configuration (iptables, ip rules).
//
// This is useful during initial setup when routes will be managed separately.
func (s *RoutingService) ApplyPersistentOnly(cfg *config.Config) error {
	log.Infof("Applying persistent configuration only...")

	if err := s.ipsetManager.CreateIfAbsent(cfg); err != nil {
		return err
	}

	if err := s.networkManager.ApplyPersistentConfig(cfg.IPSets); err != nil {
		return err
	}

	log.Infof("Persistent configuration applied successfully")
	return nil
}

// UpdateRouting updates only the routing configuration based on current interface states.
//
// This is typically called periodically or when interface states change,
// without modifying iptables rules or ip rules.
func (s *RoutingService) UpdateRouting(cfg *config.Config) error {
	log.Debugf("Updating routing configuration...")

	if err := s.networkManager.ApplyRoutingConfig(cfg.IPSets); err != nil {
		log.Errorf("Failed to update routing: %v", err)
		return err
	}

	log.Debugf("Routing updated successfully")
	return nil
}

// Undo removes all network configuration for the specified config.
//
// This includes:
//   - IP routes (both default and blackhole)
//   - IP rules for routing table selection
//   - IPTables rules for packet marking
//
// Note: This does NOT remove ipsets themselves, only the routing configuration.
func (s *RoutingService) Undo(cfg *config.Config) error {
	log.Infof("Removing routing configuration...")

	if err := s.networkManager.UndoConfig(cfg.IPSets); err != nil {
		log.Errorf("Failed to undo configuration: %v", err)
		return err
	}

	log.Infof("Routing configuration removed successfully")
	return nil
}

// filterIPSetsByInterface returns only ipsets that use the specified interface.
func (s *RoutingService) filterIPSetsByInterface(ipsets []*config.IPSetConfig, interfaceName string) []*config.IPSetConfig {
	var filtered []*config.IPSetConfig

	for _, ipset := range ipsets {
		if ipset.Routing == nil {
			continue
		}

		// Check if interface is in the ipset's interface list
		for _, iface := range ipset.Routing.Interfaces {
			if iface == interfaceName {
				filtered = append(filtered, ipset)
				break
			}
		}
	}

	return filtered
}
