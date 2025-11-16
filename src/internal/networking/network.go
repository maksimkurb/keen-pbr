package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// defaultManager is a package-level manager instance for backward compatibility.
var defaultManager = NewManager(nil)

// ApplyNetworkConfiguration applies network configuration for specified ipsets.
//
// This is a legacy function that combines persistent and routing configuration.
// For new code, use Manager.ApplyPersistentConfig() and Manager.ApplyRoutingConfig() separately.
//
// Deprecated: Use Manager instead for better control over persistent vs dynamic configuration.
func ApplyNetworkConfiguration(config *config.Config, onlyRoutingForInterface *string) (bool, error) {
	log.Infof("Applying network configuration.")

	appliedAtLeastOnce := false

	for _, ipset := range config.IPSets {
		shouldRoute := false
		if onlyRoutingForInterface == nil || *onlyRoutingForInterface == "" {
			shouldRoute = true
		} else {
			for _, interfaceName := range ipset.Routing.Interfaces {
				if interfaceName == *onlyRoutingForInterface {
					shouldRoute = true
					break
				}
			}
		}

		if !shouldRoute {
			continue
		}

		appliedAtLeastOnce = true
		if err := applyIpsetNetworkConfiguration(ipset); err != nil {
			return false, err
		}
	}

	return appliedAtLeastOnce, nil
}

// applyIpsetNetworkConfiguration applies both persistent and routing configuration for a single ipset.
//
// Deprecated: Use Manager.ApplyPersistentConfig() and Manager.ApplyRoutingConfig() separately.
func applyIpsetNetworkConfiguration(ipset *config.IPSetConfig) error {
	// Apply persistent configuration
	if err := defaultManager.persistentConfig.Apply(ipset); err != nil {
		return err
	}

	// Apply routing configuration
	if err := defaultManager.routingConfig.Apply(ipset); err != nil {
		return err
	}

	return nil
}

// ApplyPersistentNetworkConfiguration applies persistent network configuration for all ipsets.
//
// For new code, use Manager.ApplyPersistentConfig() instead.
//
// Deprecated: Use Manager.ApplyPersistentConfig() for better testability.
func ApplyPersistentNetworkConfiguration(config *config.Config) error {
	return defaultManager.ApplyPersistentConfig(config.IPSets)
}

// ApplyRoutingConfiguration updates ip routes based on current interface states.
//
// For new code, use Manager.ApplyRoutingConfig() instead.
//
// Deprecated: Use Manager.ApplyRoutingConfig() for better testability.
func ApplyRoutingConfiguration(config *config.Config) error {
	return defaultManager.ApplyRoutingConfig(config.IPSets)
}

// ChooseBestInterface selects the best available interface for the given ipset.
//
// This function queries the Keenetic API to get interface status. For new code,
// use InterfaceSelector.ChooseBest() instead.
//
// Deprecated: Use InterfaceSelector.ChooseBest() for better testability.
func ChooseBestInterface(ipset *config.IPSetConfig, keeneticIfaces map[string]keenetic.Interface) (*Interface, error) {
	// Create a temporary selector without Keenetic client (use provided map instead)
	selector := NewInterfaceSelector(nil)

	var chosenIface *Interface

	log.Infof("Choosing best interface for ipset \"%s\" from the following list: %v",
		ipset.IPSetName, ipset.Routing.Interfaces)

	for _, interfaceName := range ipset.Routing.Interfaces {
		iface, err := GetInterface(interfaceName)
		if err != nil {
			log.Errorf("Failed to get interface \"%s\" status: %v", interfaceName, err)
			continue
		}

		keeneticIface := selector.getKeeneticInterface(iface, keeneticIfaces)

		// Check if this interface should be chosen
		if chosenIface == nil && selector.IsUsable(iface, keeneticIface) {
			chosenIface = iface
		}

		// Log interface status
		selector.logInterfaceStatus(iface, selector.IsUsable(iface, keeneticIface), keeneticIface, chosenIface == iface)
	}

	if chosenIface == nil {
		log.Warnf("Could not choose best interface for ipset %s: all configured interfaces are down",
			ipset.IPSetName)
	}

	return chosenIface, nil
}
