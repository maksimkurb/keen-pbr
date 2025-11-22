package networking

import (
	"fmt"
	"net"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"golang.org/x/sys/unix"
)

// RoutingConfigManager handles dynamic routing configuration that changes
// based on interface state.
//
// Dual-Route Strategy (when kill switch enabled):
// - Blackhole route (metric 200) ALWAYS exists as permanent fallback
// - Default route (metric = interface index) added when interface available - takes precedence
// - When interface goes down, default route removed, traffic falls back to blackhole
//
// Kill Switch Disabled:
// - When all interfaces are down, removes ip rules and iptables rules
// - Allows traffic to leak to default routing instead of blocking
//
// This strategy provides:
// - Automatic failover without needing to reconfigure blackhole
// - No traffic leaks when all interfaces are down (with kill switch enabled)
// - Controlled leaks when kill switch disabled
// - Metric-based routing ensures best path is always used
//
// The manager tracks the current active interface for each ipset and only
// applies changes when the best interface actually changes, preventing
// unnecessary route churn.
type RoutingConfigManager struct {
	interfaceSelector *InterfaceSelector
	persistentConfig  *PersistentConfigManager
	mu                sync.Mutex
	activeInterfaces  map[string]string // ipset name -> active interface name (or "" for blackhole)
}

// NewRoutingConfigManager creates a new routing configuration manager.
func NewRoutingConfigManager(interfaceSelector *InterfaceSelector, persistentConfig *PersistentConfigManager) *RoutingConfigManager {
	return &RoutingConfigManager{
		interfaceSelector: interfaceSelector,
		persistentConfig:  persistentConfig,
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
	chosenIface, ifaceIndex, err := r.interfaceSelector.ChooseBest(ipset)
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

	// Log appropriately based on whether this is first time or a change
	if !exists {
		// First time - interface is being selected
		log.Infof("[ipset %s] Selected interface: %s",
			ipset.IPSetName,
			ifaceNameOrBlackhole(targetIface))
	} else {
		// Interface changed from previous selection
		log.Infof("[ipset %s] Interface changed: %s -> %s, updating routes",
			ipset.IPSetName,
			ifaceNameOrBlackhole(currentIface),
			ifaceNameOrBlackhole(targetIface))
	}

	if err := r.applyRoutes(ipset, chosenIface, ifaceIndex); err != nil {
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
// - Default route (metric = interface index) is added when interface is available - takes precedence
// - Old default routes are cleaned up (but blackhole is preserved)
//
// For periodic monitoring, use ApplyIfChanged instead to avoid unnecessary updates.
func (r *RoutingConfigManager) Apply(ipset *config.IPSetConfig) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Choose the best interface
	chosenIface, ifaceIndex, err := r.interfaceSelector.ChooseBest(ipset)
	if err != nil {
		return err
	}

	// Determine target interface name
	var targetIface string
	if chosenIface == nil {
		targetIface = ""
	} else {
		targetIface = chosenIface.Attrs().Name
	}

	// Log interface selection
	// Check if this is first time or if interface changed
	currentIface, exists := r.activeInterfaces[ipset.IPSetName]
	if !exists {
		// First time - interface is being selected
		log.Infof("[ipset %s] Selected interface: %s",
			ipset.IPSetName,
			ifaceNameOrBlackhole(targetIface))
	} else if currentIface != targetIface {
		// Interface changed from previous selection
		log.Infof("[ipset %s] Interface changed: %s -> %s, updating routes",
			ipset.IPSetName,
			ifaceNameOrBlackhole(currentIface),
			ifaceNameOrBlackhole(targetIface))
	}

	// Apply routes
	if err := r.applyRoutes(ipset, chosenIface, ifaceIndex); err != nil {
		return err
	}

	// Update tracked state
	r.activeInterfaces[ipset.IPSetName] = targetIface

	return nil
}

// applyRoutes is the internal method that actually applies routes.
// Must be called with mutex held.
//
// This method implements a dual-route strategy with minimal downtime:
//  1. Blackhole route ALWAYS exists with higher metric (1000) as fallback
//  2. When interface is available, default route exists with lower metric (100 + ipset index) - takes precedence
//  3. Routes are updated in order: ensure blackhole → add new route → remove old routes
//  4. Special case: single-interface ipsets never remove their route (avoid unnecessary churn)
//
// ifaceIndex is the 0-based position of the chosen interface in the ipset's interface list.
// It is -1 if no interface was chosen.
func (r *RoutingConfigManager) applyRoutes(ipset *config.IPSetConfig, chosenIface *Interface, ifaceIndex int) error {
	blackholePresent := false
	var existingRoutes []*IPRoute

	// Step 1: List existing routes
	if routes, err := ListRoutesInTable(ipset.Routing.IPRouteTable); err != nil {
		return err
	} else {
		for _, route := range routes {
			if route.Type&unix.RTN_BLACKHOLE != 0 {
				blackholePresent = true
			} else {
				existingRoutes = append(existingRoutes, route)
			}
		}
	}

	// Step 2: Handle kill switch behavior
	killSwitchEnabled := ipset.Routing.IsKillSwitchEnabled()

	// Step 3: Add new default gateway route if interface available (metric 100, takes precedence)
	// This happens BEFORE removing old routes to minimize downtime
	if chosenIface != nil {
		// Interface is available - ensure persistent config is applied
		if err := r.persistentConfig.Apply(ipset); err != nil {
			return err
		}

		// Ensure blackhole route exists (if kill switch enabled)
		if killSwitchEnabled && !blackholePresent {
			if err := r.addBlackholeRoute(ipset); err != nil {
				return err
			}
		}

		if err := r.addDefaultGatewayRoute(ipset, chosenIface, ifaceIndex); err != nil {
			return err
		}

		// Step 4: Clean up old routes (now that new route is in place)
		for _, route := range existingRoutes {
			// Skip if this is the route we just added (same interface)
			if route.LinkIndex == chosenIface.Link.Attrs().Index {
				continue
			}
			if deleted, err := route.DelIfExists(); err != nil {
				return err
			} else if deleted {
				log.Infof("[%s] Removed old route via interface idx=%d", ipset.IPSetName, route.LinkIndex)
			}
		}
	} else if defaultGateway := ipset.Routing.DefaultGateway; defaultGateway != "" {
		// No interface available but default gateway is configured
		gateway := net.ParseIP(defaultGateway)
		if gateway == nil {
			return fmt.Errorf("invalid default gateway IP: %s", defaultGateway)
		}

		log.Infof("[%s] No interface available, using default gateway %s", ipset.IPSetName, defaultGateway)

		// Apply persistent config (iptables/ip rules)
		if err := r.persistentConfig.Apply(ipset); err != nil {
			return err
		}

		// Ensure blackhole route exists if kill switch enabled (as fallback)
		if killSwitchEnabled && !blackholePresent {
			if err := r.addBlackholeRoute(ipset); err != nil {
				return err
			}
		}

		// Add gateway route
		if err := r.addGatewayRoute(ipset, gateway); err != nil {
			return err
		}

		// Clean up old interface-based routes
		for _, route := range existingRoutes {
			if deleted, err := route.DelIfExists(); err != nil {
				return err
			} else if deleted {
				log.Infof("[%s] Removed old route", ipset.IPSetName)
			}
		}
	} else {
		// No interface available and no default gateway - handle based on kill switch setting
		if !killSwitchEnabled {
			// Kill switch disabled: remove all routing config to allow traffic leaks
			log.Infof("[%s] Kill switch disabled and no interface available, removing routing config (traffic will leak)", ipset.IPSetName)

			// Remove all routes from the routing table
			if err := DelIPRouteTable(ipset.Routing.IPRouteTable); err != nil {
				return err
			}

			// Remove persistent config (ip rules and iptables rules)
			if err := r.persistentConfig.Remove(ipset); err != nil {
				return err
			}
		} else {
			// Kill switch enabled: keep blackhole route and persistent config
			// Ensure blackhole route exists (permanent fallback with metric 200)
			if !blackholePresent {
				if err := r.addBlackholeRoute(ipset); err != nil {
					return err
				}
			}

			// Special case: if only ONE interface configured, keep its route even if down
			// This avoids unnecessary route churn when the interface is temporarily unavailable
			if len(ipset.Routing.Interfaces) == 1 {
				// Keep existing routes, don't remove them
				log.Debugf("[%s] Only one interface configured, keeping existing route (avoid churn)", ipset.IPSetName)
			} else {
				// Multiple interfaces available: remove all default routes, fall back to blackhole
				anyRemoved := false
				for _, route := range existingRoutes {
					if deleted, err := route.DelIfExists(); err != nil {
						return err
					} else if deleted {
						anyRemoved = true
					}
				}
				if anyRemoved {
					log.Infof("[%s] No interface available, removed default routes (using blackhole)", ipset.IPSetName)
				}
			}
		}
	}

	return nil
}

// addDefaultGatewayRoute adds a default route through the specified interface.
// ifaceIndex is the 0-based position of the interface in the ipset's interface list.
func (r *RoutingConfigManager) addDefaultGatewayRoute(ipset *config.IPSetConfig, chosenIface *Interface, ifaceIndex int) error {
	ipRoute := BuildDefaultRoute(ipset.IPVersion, *chosenIface, ipset.Routing.IPRouteTable, ifaceIndex)
	if added, err := ipRoute.AddIfNotExists(); err != nil {
		return err
	} else if added {
		log.Infof("[%s] Added default route via %s to table %d (metric %d)",
			ipset.IPSetName, chosenIface.Attrs().Name, ipset.Routing.IPRouteTable, ipRoute.Priority)
	}
	return nil
}

// addBlackholeRoute adds a blackhole route as permanent fallback (metric 1000).
//
// This route always exists and acts as a safety net when no interface is available.
// Because it has a higher metric than typical default routes (1000 vs 100-199 for interfaces, 500 for gateway),
// it will only be used when the default route is absent.
func (r *RoutingConfigManager) addBlackholeRoute(ipset *config.IPSetConfig) error {
	route := BuildBlackholeRoute(ipset.IPVersion, ipset.Routing.IPRouteTable)
	if added, err := route.AddIfNotExists(); err != nil {
		return err
	} else if added {
		log.Infof("[%s] Added blackhole route to table %d (metric 1000, permanent fallback)",
			ipset.IPSetName, ipset.Routing.IPRouteTable)
	}
	return nil
}

// addGatewayRoute adds a default route through the specified gateway IP.
func (r *RoutingConfigManager) addGatewayRoute(ipset *config.IPSetConfig, gateway net.IP) error {
	ipRoute := BuildDefaultRouteViaGateway(ipset.IPVersion, gateway, ipset.Routing.IPRouteTable)
	if added, err := ipRoute.AddIfNotExists(); err != nil {
		return err
	} else if added {
		log.Infof("[%s] Added default route via gateway %s to table %d (metric %d)",
			ipset.IPSetName, gateway.String(), ipset.Routing.IPRouteTable, ipRoute.Priority)
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
