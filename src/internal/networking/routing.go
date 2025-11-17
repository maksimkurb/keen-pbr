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
// Dual-Route Strategy:
// - Blackhole route (metric 200) ALWAYS exists as permanent fallback
// - Default route (metric 100) added when interface available - takes precedence
// - When interface goes down, default route removed, traffic falls back to blackhole
//
// This strategy provides:
// - Automatic failover without needing to reconfigure blackhole
// - No traffic leaks when all interfaces are down
// - Metric-based routing ensures best path is always used
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
// This method implements a dual-route strategy:
// - Blackhole route (metric 200) is ALWAYS present as fallback
// - Default route (metric 100) is added when interface is available - takes precedence
// - Old default routes are cleaned up (but blackhole is preserved)
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
//
// This method implements a dual-route strategy for reliability:
//  1. Blackhole route ALWAYS exists with higher metric (200) as fallback
//  2. When interface is available, default route exists with lower metric (100) - takes precedence
//  3. When interface unavailable, only blackhole route remains - prevents traffic leaks
func (r *RoutingConfigManager) applyRoutes(ipset *config.IPSetConfig, chosenIface *Interface) error {
	blackholePresent := false

	// List and clean up existing default routes (but preserve blackhole)
	if routes, err := ListRoutesInTable(ipset.Routing.IpRouteTable); err != nil {
		return err
	} else {
		for _, route := range routes {
			// Keep blackhole route - it's our permanent fallback
			if route.Type&unix.RTN_BLACKHOLE != 0 {
				blackholePresent = true
				continue
			}

			// Remove old default routes (will be re-added if interface available)
			if err := route.DelIfExists(); err != nil {
				return err
			}
		}
	}

	// Step 1: Ensure blackhole route ALWAYS exists (permanent fallback with metric 200)
	if !blackholePresent {
		log.Infof("Ensuring blackhole route exists for ipset [%s] (metric 200, fallback)", ipset.IPSetName)
		if err := r.addBlackholeRoute(ipset); err != nil {
			return err
		}
	}

	// Step 2: Add default gateway route if interface available (metric 100, takes precedence)
	if chosenIface != nil {
		log.Infof("Adding default gateway route for ipset [%s] via %s (metric 100, active)",
			ipset.IPSetName, chosenIface.Attrs().Name)
		if err := r.addDefaultGatewayRoute(ipset, chosenIface); err != nil {
			return err
		}
	} else {
		log.Infof("No interface available for ipset [%s], traffic will use blackhole route (metric 200)",
			ipset.IPSetName)
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

// addBlackholeRoute adds a blackhole route as permanent fallback (metric 200).
//
// This route always exists and acts as a safety net when no interface is available.
// Because it has a higher metric than the default route (200 vs 100), it will only
// be used when the default route is absent.
func (r *RoutingConfigManager) addBlackholeRoute(ipset *config.IPSetConfig) error {
	log.Infof("Adding blackhole ip route to table=%d (metric 200, permanent fallback)",
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
