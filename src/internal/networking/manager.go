package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// Manager is the main facade for network configuration management.
//
// It orchestrates persistent configuration (iptables, ip rules) and
// dynamic routing configuration (ip routes) through specialized managers.
//
// The manager implements the NetworkManager interface from the domain package,
// providing a clean API for applying, updating, and removing network configuration.
type Manager struct {
	persistentConfig  *PersistentConfigManager
	routingConfig     *RoutingConfigManager
	interfaceSelector *InterfaceSelector
}

// NewManager creates a new network configuration manager.
//
// The keeneticClient parameter can be nil if Keenetic API integration is not available.
// In this case, only system-level interface status will be used for routing decisions.
func NewManager(keeneticClient *keenetic.Client) *Manager {
	interfaceSelector := NewInterfaceSelector(keeneticClient)
	persistentConfig := NewPersistentConfigManager()

	return &Manager{
		persistentConfig:  persistentConfig,
		routingConfig:     NewRoutingConfigManager(interfaceSelector, persistentConfig),
		interfaceSelector: interfaceSelector,
	}
}

// ApplyPersistentConfig applies persistent network configuration for all ipsets.
//
// This includes iptables rules and ip rules that should remain active
// regardless of interface state. This method should be called once on
// service start.
func (m *Manager) ApplyPersistentConfig(ipsets []*config.IPSetConfig) error {
	log.Infof("Applying persistent network configuration (iptables rules and ip rules)...")

	for _, ipset := range ipsets {
		if err := m.persistentConfig.Apply(ipset); err != nil {
			return err
		}
	}

	return nil
}

// ApplyRoutingConfig applies dynamic routing configuration for all ipsets.
//
// This updates ip routes based on current interface states. This method
// should be called periodically to adjust routes when interfaces go up/down.
func (m *Manager) ApplyRoutingConfig(ipsets []*config.IPSetConfig) error {
	log.Debugf("Updating routing configuration based on interface states...")

	for _, ipset := range ipsets {
		if err := m.routingConfig.Apply(ipset); err != nil {
			return err
		}
	}

	return nil
}

// UpdateRoutingIfChanged updates routing configuration only for ipsets where
// the best interface has changed.
//
// This is the preferred method for periodic monitoring as it prevents unnecessary
// route churn. It's thread-safe and can be called concurrently from multiple
// sources (ticker, SIGHUP handler, etc.).
//
// Returns the number of ipsets that were actually updated.
func (m *Manager) UpdateRoutingIfChanged(ipsets []*config.IPSetConfig) (int, error) {
	updatedCount := 0

	for _, ipset := range ipsets {
		updated, err := m.routingConfig.ApplyIfChanged(ipset)
		if err != nil {
			return updatedCount, err
		}
		if updated {
			updatedCount++
		}
	}

	if updatedCount > 0 {
		log.Infof("Updated routing for %d ipset(s)", updatedCount)
	} else {
		log.Debugf("No routing changes needed, all interfaces unchanged")
	}

	return updatedCount, nil
}

// UndoConfig removes all network configuration for the specified ipsets.
//
// This includes:
// - IP routes (both default and blackhole)
// - IP rules for routing table selection
// - IPTables rules for packet marking
func (m *Manager) UndoConfig(ipsets []*config.IPSetConfig) error {
	log.Infof("Removing all iptables rules, ip rules and ip routes...")

	for _, ipset := range ipsets {
		// Delete IP routes
		log.Infof("Deleting IP route table %d", ipset.Routing.IpRouteTable)
		if err := DelIpRouteTable(ipset.Routing.IpRouteTable); err != nil {
			return err
		}

		// Delete persistent configuration (ip rules and iptables rules)
		if err := m.persistentConfig.Remove(ipset); err != nil {
			return err
		}
	}

	log.Infof("Undo routing completed successfully")
	return nil
}
