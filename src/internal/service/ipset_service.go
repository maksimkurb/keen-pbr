package service

import (
	"net/netip"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// IPSetService orchestrates ipset operations including list downloads and imports.
//
// It coordinates between list downloads, network parsing, and ipset management,
// providing a high-level API for populating ipsets with networks from various sources.
type IPSetService struct {
	ipsetManager *networking.IPSetManagerImpl
}

// NewIPSetService creates a new ipset service.
//
// Parameters:
//   - ipsetManager: Handles ipset creation and population
func NewIPSetService(ipsetManager *networking.IPSetManagerImpl) *IPSetService {
	return &IPSetService{
		ipsetManager: ipsetManager,
	}
}

// EnsureIPSetsExist creates all ipsets defined in the configuration if they don't exist.
//
// This is idempotent - if ipsets already exist, this is a no-op.
func (s *IPSetService) EnsureIPSetsExist(cfg *config.Config) error {
	log.Infof("Ensuring ipsets exist...")

	if err := s.ipsetManager.CreateIfAbsent(cfg); err != nil {
		log.Errorf("Failed to create ipsets: %v", err)
		return err
	}

	log.Infof("All ipsets exist")
	return nil
}

// PopulateIPSets downloads lists and populates all ipsets with networks.
//
// For each ipset:
//  1. Downloads all configured lists
//  2. Parses networks from lists
//  3. Optionally flushes the ipset (if FlushBeforeApplying is true)
//  4. Imports networks into the ipset
//
// Returns error if any operation fails.
func (s *IPSetService) PopulateIPSets(cfg *config.Config) error {
	log.Infof("Populating ipsets...")

	// First download all lists
	if err := lists.DownloadLists(cfg); err != nil {
		return err
	}

	// Then populate each ipset
	for _, ipsetCfg := range cfg.IPSets {
		if err := s.PopulateIPSet(cfg, ipsetCfg); err != nil {
			return err
		}
	}

	log.Infof("All ipsets populated successfully")
	return nil
}

// PopulateIPSet populates a single ipset with networks from its configured lists.
func (s *IPSetService) PopulateIPSet(cfg *config.Config, ipsetCfg *config.IPSetConfig) error {
	log.Infof("Populating ipset %s...", ipsetCfg.IPSetName)

	// Get networks from all lists for this ipset
	networks, err := s.getNetworksForIPSet(cfg, ipsetCfg)
	if err != nil {
		log.Errorf("Failed to get networks for ipset %s: %v", ipsetCfg.IPSetName, err)
		return err
	}

	log.Infof("Found %d networks for ipset %s", len(networks), ipsetCfg.IPSetName)

	// Flush ipset if configured
	if ipsetCfg.FlushBeforeApplying {
		log.Debugf("Flushing ipset %s before applying...", ipsetCfg.IPSetName)
		if err := s.ipsetManager.Flush(ipsetCfg.IPSetName); err != nil {
			log.Errorf("Failed to flush ipset %s: %v", ipsetCfg.IPSetName, err)
			return err
		}
	}

	// Import networks into ipset
	if err := s.ipsetManager.Import(ipsetCfg, networks); err != nil {
		log.Errorf("Failed to import networks into ipset %s: %v", ipsetCfg.IPSetName, err)
		return err
	}

	log.Infof("Ipset %s populated successfully", ipsetCfg.IPSetName)
	return nil
}

// DownloadLists downloads all lists defined in the configuration.
//
// This downloads lists to the configured output directory without populating ipsets.
// Useful for pre-downloading lists before applying configuration.
func (s *IPSetService) DownloadLists(cfg *config.Config) error {
	log.Infof("Downloading lists...")
	return lists.DownloadLists(cfg)
}

// getNetworksForIPSet retrieves all networks for an ipset from its configured lists.
func (s *IPSetService) getNetworksForIPSet(cfg *config.Config, ipsetCfg *config.IPSetConfig) ([]netip.Prefix, error) {
	var allNetworks []netip.Prefix

	for _, listName := range ipsetCfg.Lists {
		// Find list config by name
		var listCfg *config.ListSource
		for _, l := range cfg.Lists {
			if l.ListName == listName {
				listCfg = l
				break
			}
		}

		if listCfg == nil {
			log.Warnf("List %s referenced by ipset %s but not defined in config", listName, ipsetCfg.IPSetName)
			continue
		}

		// Parse networks from list
		networks, err := lists.GetNetworksFromList(listCfg, cfg)
		if err != nil {
			log.Errorf("Failed to get networks from list %s: %v", listName, err)
			return nil, err
		}

		allNetworks = append(allNetworks, networks...)
	}

	return allNetworks, nil
}
