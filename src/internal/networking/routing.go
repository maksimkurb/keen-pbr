package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"golang.org/x/sys/unix"
)

// RoutingConfigManager handles dynamic routing configuration that changes
// based on interface state.
//
// This includes:
// - Default routes through VPN interfaces
// - Blackhole routes when all interfaces are down
//
// Routes are updated dynamically as interfaces go up and down to ensure
// traffic always uses the best available interface.
type RoutingConfigManager struct {
	interfaceSelector *InterfaceSelector
}

// NewRoutingConfigManager creates a new routing configuration manager.
func NewRoutingConfigManager(interfaceSelector *InterfaceSelector) *RoutingConfigManager {
	return &RoutingConfigManager{
		interfaceSelector: interfaceSelector,
	}
}

// Apply applies routing configuration for a single ipset.
//
// This method cleans up existing routes (except blackhole) and adds either:
// - A default route through the best available interface, OR
// - A blackhole route if no interface is available
func (r *RoutingConfigManager) Apply(ipset *config.IPSetConfig) error {
	log.Debugf("Updating routes for ipset [%s]", ipset.IPSetName)

	blackholePresent := false

	// List and clean up existing routes (except blackhole)
	if routes, err := ListRoutesInTable(ipset.Routing.IpRouteTable); err != nil {
		return err
	} else {
		for _, route := range routes {
			if route.Type&unix.RTN_BLACKHOLE != 0 {
				blackholePresent = true
				continue
			}

			if err := route.DelIfExists(); err != nil {
				return err
			}
		}
	}

	// Choose the best interface
	chosenIface, err := r.interfaceSelector.ChooseBest(ipset)
	if err != nil {
		return err
	}

	// Add appropriate route based on interface availability
	if chosenIface == nil {
		// No interface available - add blackhole route to prevent traffic leaks
		log.Infof("No interface available for ipset [%s], adding blackhole route", ipset.IPSetName)
		if !blackholePresent {
			if err := r.addBlackholeRoute(ipset); err != nil {
				return err
			}
		}
	} else {
		// Interface available - add default gateway route
		if err := r.addDefaultGatewayRoute(ipset, chosenIface); err != nil {
			return err
		}
	}

	return nil
}

// addDefaultGatewayRoute adds a default route through the specified interface.
func (r *RoutingConfigManager) addDefaultGatewayRoute(ipset *config.IPSetConfig, chosenIface *Interface) error {
	log.Infof("Adding default gateway ip route dev=%s to table=%d",
		chosenIface.Attrs().Name, ipset.Routing.IpRouteTable)

	ipRoute := BuildDefaultRoute(ipset.IPVersion, *chosenIface, ipset.Routing.IpRouteTable)
	if err := ipRoute.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}

// addBlackholeRoute adds a blackhole route to prevent traffic leaks when no interface is available.
func (r *RoutingConfigManager) addBlackholeRoute(ipset *config.IPSetConfig) error {
	log.Infof("Adding blackhole ip route to table=%d to prevent traffic leaks (all interfaces down)",
		ipset.Routing.IpRouteTable)

	route := BuildBlackholeRoute(ipset.IPVersion, ipset.Routing.IpRouteTable)
	if err := route.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}
