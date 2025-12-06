package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// PersistentConfigManager handles persistent network configuration that should
// remain active regardless of interface state.
//
// This includes:
// - IPTables rules for packet marking and filtering
// - IP rules for routing table selection based on fwmark
//
// These configurations are applied once and kept active even when VPN
// interfaces go down, ensuring traffic is blocked rather than leaked.
type PersistentConfigManager struct{}

// NewPersistentConfigManager creates a new persistent configuration manager.
func NewPersistentConfigManager() *PersistentConfigManager {
	return &PersistentConfigManager{}
}

// Apply applies persistent configuration for a single ipset.
//
// This creates iptables rules for packet marking and ip rules for routing
// table selection. These rules stay active regardless of interface state.
func (p *PersistentConfigManager) Apply(ipset *config.IPSetConfig) error {
	ipRule := BuildIPRuleFromConfig(ipset)
	ipTableRules, err := BuildIPTablesRules(ipset)
	if err != nil {
		return err
	}

	// Always ensure iptables rules and ip rules are present
	if added, err := ipRule.AddIfNotExists(); err != nil {
		return err
	} else if added {
		log.Infof("[ipset %s] Added ip rule: fwmark=%d -> table=%d (priority=%d)",
			ipset.IPSetName, ipset.Routing.FwMark, ipset.Routing.IPRouteTable, ipset.Routing.IPRulePriority)
	}

	if added, err := ipTableRules.AddIfNotExists(); err != nil {
		return err
	} else if added {
		log.Infof("[ipset %s] Added iptables rules for packet marking", ipset.IPSetName)
	}

	return nil
}

// Remove removes persistent configuration for a single ipset.
//
// This deletes all iptables rules and ip rules associated with the ipset.
func (p *PersistentConfigManager) Remove(ipset *config.IPSetConfig) error {
	ipRule := BuildIPRuleFromConfig(ipset)

	if deleted, err := ipRule.DelIfExists(); err != nil {
		log.Errorf("[ipset %s] Failed to delete IP rule: %v", ipset.IPSetName, err)
		return err
	} else if deleted {
		log.Infof("[ipset %s] Deleted ip rule: fwmark=%d -> table=%d",
			ipset.IPSetName, ipset.Routing.FwMark, ipset.Routing.IPRouteTable)
	}

	if ipTableRules, err := BuildIPTablesRules(ipset); err != nil {
		log.Errorf("[ipset %s] Failed to build iptables rules: %v", ipset.IPSetName, err)
		return err
	} else {
		if deleted, err := ipTableRules.DelIfExists(); err != nil {
			log.Errorf("[ipset %s] Failed to delete iptables rules: %v", ipset.IPSetName, err)
			return err
		} else if deleted {
			log.Infof("[ipset %s] Deleted iptables rules", ipset.IPSetName)
		}
	}

	return nil
}
