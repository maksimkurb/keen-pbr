package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// Manager is the main facade for network configuration management.
//
// It orchestrates persistent configuration (iptables, ip rules, dns redirect) and
// dynamic routing configuration (ip routes) through specialized managers.
//
// The manager implements the NetworkManager interface from the domain package,
// providing a clean API for applying, updating, and removing network configuration.
type Manager struct {
	persistentConfig  *PersistentConfigManager
	routingConfig     *RoutingConfigManager
	interfaceSelector *InterfaceSelector

	// Global config for service-level components (DNS redirect, etc.)
	globalConfig GlobalConfig
}

// NewManager creates a new network configuration manager.
//
// The keeneticClient parameter can be nil if Keenetic API integration is not available.
// In this case, only system-level interface status will be used for routing decisions.
func NewManager(keeneticClient InterfaceLister) *Manager {
	interfaceSelector := NewInterfaceSelector(keeneticClient)
	persistentConfig := NewPersistentConfigManager()

	return &Manager{
		persistentConfig:  persistentConfig,
		routingConfig:     NewRoutingConfigManager(interfaceSelector, persistentConfig),
		interfaceSelector: interfaceSelector,
	}
}

// SetGlobalConfig sets the global configuration for service-level components.
// This should be called before ApplyNetfilter to enable global components
// like DNS redirect.
func (m *Manager) SetGlobalConfig(globalCfg GlobalConfig) {
	m.globalConfig = globalCfg
}

// GetGlobalConfig returns the current global configuration.
func (m *Manager) GetGlobalConfig() GlobalConfig {
	return m.globalConfig
}

// ApplyRouting applies routing configuration (ip rules and ip routes) for all ipsets.
// When force=false: only updates routes if interfaces changed (efficient for periodic monitoring)
// When force=true: unconditionally applies all routing (for reload/startup)
// Returns the number of ipsets that were updated.
func (m *Manager) ApplyRouting(ipsets []*config.IPSetConfig, force bool) (int, error) {
	updatedCount := 0

	for _, ipset := range ipsets {
		// Build components for this ipset
		builder := NewComponentBuilderWithSelector(m.interfaceSelector)
		components, err := builder.BuildComponents(ipset)
		if err != nil {
			return updatedCount, err
		}

		// Always ensure IPSet and IP Rules exist (persistent components)
		for _, component := range components {
			compType := component.GetType()
			if compType == ComponentTypeIPSet || compType == ComponentTypeIPRule {
				if component.ShouldExist() {
					if err := component.CreateIfNotExists(); err != nil {
						log.Errorf("[ipset %s] Failed to create %s: %v", ipset.IPSetName, compType, err)
						return updatedCount, err
					}
				}
			}
		}

		// Apply routes conditionally based on force flag
		if force {
			// Force mode: unconditionally apply routes
			log.Infof("[ipset %s] Applying routing configuration (forced)...", ipset.IPSetName)
			if err := m.routingConfig.Apply(ipset); err != nil {
				return updatedCount, err
			}
			updatedCount++
		} else {
			// Efficient mode: only update if interfaces changed
			updated, err := m.routingConfig.ApplyIfChanged(ipset)
			if err != nil {
				return updatedCount, err
			}
			if updated {
				updatedCount++
			}
		}
	}

	if updatedCount > 0 {
		log.Debugf("Updated routing for %d ipset(s)", updatedCount)
	} else if !force {
		log.Debugf("No routing changes needed, all interfaces unchanged")
	}

	return updatedCount, nil
}

// ApplyNetfilter applies netfilter configuration (iptables rules and global components)
// for all ipsets. This includes iptables rules for packet marking and DNS redirect rules.
func (m *Manager) ApplyNetfilter(ipsets []*config.IPSetConfig) error {
	// Build and apply global components first (DNS redirect)
	globalBuilder := NewGlobalComponentBuilder()
	globalComponents, err := globalBuilder.BuildComponents(m.globalConfig)
	if err != nil {
		return err
	}

	for _, component := range globalComponents {
		if component.ShouldExist() {
			log.Infof("[global] Applying %s...", component.GetDescription())
			if err := component.CreateIfNotExists(); err != nil {
				log.Errorf("[global] Failed to create %s: %v", component.GetType(), err)
				return err
			}
		}
	}

	// Apply per-ipset iptables rules
	for _, ipset := range ipsets {
		log.Infof("[ipset %s] Applying netfilter configuration (iptables rules)...", ipset.IPSetName)

		builder := NewComponentBuilderWithSelector(m.interfaceSelector)
		components, err := builder.BuildComponents(ipset)
		if err != nil {
			return err
		}

		// Apply only iptables components
		for _, component := range components {
			if component.GetType() == ComponentTypeIPTables {
				if component.ShouldExist() {
					if err := component.CreateIfNotExists(); err != nil {
						log.Errorf("[ipset %s] Failed to create iptables rules: %v", ipset.IPSetName, err)
						return err
					}
				}
			}
		}
	}

	return nil
}

// UndoConfig removes all network configuration for the specified ipsets.
//
// This includes:
// - IP routes (both default and blackhole)
// - IP rules for routing table selection
// - IPTables rules for packet marking
// - Global components (DNS redirect)
//
// This implementation uses the NetworkingComponent abstraction for unified logic.
func (m *Manager) UndoConfig(ipsets []*config.IPSetConfig) error {
	log.Infof("Removing all iptables rules, ip rules, ip routes, and global components...")

	// Remove per-ipset components first
	for _, ipset := range ipsets {
		// Build all components for this ipset
		builder := NewComponentBuilderWithSelector(m.interfaceSelector)
		components, err := builder.BuildComponents(ipset)
		if err != nil {
			log.Warnf("Failed to build components for %s during undo: %v", ipset.IPSetName, err)
			// Continue with fallback method
			if err := DelIPRouteTable(ipset.Routing.IPRouteTable); err != nil {
				return err
			}
			if err := m.persistentConfig.Remove(ipset); err != nil {
				return err
			}
			continue
		}

		// Delete all components (except IPSet itself)
		for _, component := range components {
			// Skip IPSet deletion - we don't want to remove the ipset itself
			if component.GetType() == ComponentTypeIPSet {
				continue
			}

			if err := component.DeleteIfExists(); err != nil {
				log.Warnf("Failed to delete %s for %s: %v", component.GetType(), ipset.IPSetName, err)
				// Continue with other components
			}
		}
	}

	// Remove global components (DNS redirect)
	globalBuilder := NewGlobalComponentBuilder()
	globalComponents, err := globalBuilder.BuildComponents(m.globalConfig)
	if err != nil {
		log.Warnf("Failed to build global components during undo: %v", err)
	} else {
		for _, component := range globalComponents {
			log.Infof("[global] Removing %s...", component.GetDescription())
			if err := component.DeleteIfExists(); err != nil {
				log.Warnf("[global] Failed to delete %s: %v", component.GetType(), err)
				// Continue with other components
			}
		}
	}

	log.Infof("Undo routing completed successfully")
	return nil
}
