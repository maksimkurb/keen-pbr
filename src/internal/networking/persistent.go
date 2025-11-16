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
	log.Infof("----------------- IPSet [%s] - Applying Persistent Config ------------------",
		ipset.IPSetName)

	ipRule := NewIPRuleBuilder(ipset).Build()
	ipTableRules, err := NewIPTablesBuilder(ipset).Build()
	if err != nil {
		return err
	}

	// Always ensure iptables rules and ip rules are present
	log.Infof("Adding ip rule to forward all packets with fwmark=%d (ipset=%s) to table=%d (priority=%d)",
		ipset.Routing.FwMark, ipset.IPSetName, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)

	if err := ipRule.AddIfNotExists(); err != nil {
		return err
	}

	if err := ipTableRules.AddIfNotExists(); err != nil {
		return err
	}

	log.Infof("----------------- IPSet [%s] - Persistent Config Applied ------------------",
		ipset.IPSetName)
	return nil
}

// Remove removes persistent configuration for a single ipset.
//
// This deletes all iptables rules and ip rules associated with the ipset.
func (p *PersistentConfigManager) Remove(ipset *config.IPSetConfig) error {
	log.Infof("----------------- IPSet [%s] - Removing Persistent Config ------------------",
		ipset.IPSetName)

	ipRule := NewIPRuleBuilder(ipset).Build()
	log.Infof("Deleting IP rule [%v]", ipRule)

	if exists, err := ipRule.IsExists(); err != nil {
		log.Errorf("Failed to check IP rule [%v]: %v", ipRule, err)
		return err
	} else if exists {
		if err := ipRule.DelIfExists(); err != nil {
			log.Errorf("Failed to delete IP rule [%v]: %v", ipRule, err)
			return err
		}
	} else {
		log.Infof("IP rule [%v] does not exist, skipping", ipRule)
	}

	log.Infof("Deleting iptables rules")
	if ipTableRules, err := NewIPTablesBuilder(ipset).Build(); err != nil {
		log.Errorf("Failed to build iptables rules: %v", err)
		return err
	} else {
		if err := ipTableRules.DelIfExists(); err != nil {
			log.Errorf("Failed to delete iptables rules [%v]: %v", ipTableRules, err)
			return err
		}
	}

	log.Infof("----------------- IPSet [%s] - Persistent Config Removed ------------------",
		ipset.IPSetName)
	return nil
}
