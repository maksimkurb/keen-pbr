package networking

import (
	"sync"

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
//
// The manager tracks the current active interface for each ipset and only
// applies changes when the best interface actually changes, preventing
// unnecessary route churn.
type RoutingConfigManager struct {
	interfaceSelector *InterfaceSelector
	mu                sync.Mutex
	activeInterfaces  map[string]string // ipset name -> active interface name (or "" for blackhole)
}

// NewRoutingConfigManager creates a new routing configuration manager.
func NewRoutingConfigManager(interfaceSelector *InterfaceSelector) *RoutingConfigManager {
	return &RoutingConfigManager{
		interfaceSelector: interfaceSelector,
		activeInterfaces:  make(map[string]string),
	}
}

// ApplyIfChanged applies routing configuration for a single ipset only if
// the best interface has changed since the last application.
//
// This method is thread-safe and prevents unnecessary route updates when
// the interface state hasn't changed. It's designed to be called periodically
// from both the ticker and SIGHUP handler without causing route churn.
//
// Returns true if routes were updated, false if no change was needed.
func (r *RoutingConfigManager) ApplyIfChanged(ipset *config.IPSetConfig) (bool, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Choose the best interface
	chosenIface, err := r.interfaceSelector.ChooseBest(ipset)
	if err != nil {
		return false, err
	}

	// Determine target interface name
	var targetIface string
	if chosenIface == nil {
		targetIface = "" // blackhole
	} else {
		targetIface = chosenIface.Attrs().Name
	}

	// Check if interface has changed
	currentIface, exists := r.activeInterfaces[ipset.IPSetName]
	if exists && currentIface == targetIface {
		log.Debugf("Interface for ipset [%s] unchanged (%s), skipping route update",
			ipset.IPSetName, ifaceNameOrBlackhole(targetIface))
		return false, nil
	}

	// Interface changed - apply new routes
	log.Infof("Interface for ipset [%s] changed: %s -> %s, updating routes",
		ipset.IPSetName,
		ifaceNameOrBlackhole(currentIface),
		ifaceNameOrBlackhole(targetIface))

	if err := r.applyRoutes(ipset, chosenIface); err != nil {
		return false, err
	}

	// Update tracked state
	r.activeInterfaces[ipset.IPSetName] = targetIface
	return true, nil
}

// Apply applies routing configuration for a single ipset unconditionally.
//
// This method cleans up existing routes (except blackhole) and adds either:
// - A default route through the best available interface, OR
// - A blackhole route if no interface is available
//
// For periodic monitoring, use ApplyIfChanged instead to avoid unnecessary updates.
func (r *RoutingConfigManager) Apply(ipset *config.IPSetConfig) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Choose the best interface
	chosenIface, err := r.interfaceSelector.ChooseBest(ipset)
	if err != nil {
		return err
	}

	// Apply routes
	if err := r.applyRoutes(ipset, chosenIface); err != nil {
		return err
	}

	// Update tracked state
	var targetIface string
	if chosenIface == nil {
		targetIface = ""
	} else {
		targetIface = chosenIface.Attrs().Name
	}
	r.activeInterfaces[ipset.IPSetName] = targetIface

	return nil
}

// applyRoutes is the internal method that actually applies routes.
// Must be called with mutex held.
func (r *RoutingConfigManager) applyRoutes(ipset *config.IPSetConfig, chosenIface *Interface) error {
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

// ifaceNameOrBlackhole returns a friendly name for logging.
func ifaceNameOrBlackhole(ifaceName string) string {
	if ifaceName == "" {
		return "blackhole"
	}
	return ifaceName
}
