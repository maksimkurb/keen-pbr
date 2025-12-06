package networking

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// RouteType identifies the type of routing entry
type RouteType string

const (
	RouteTypeDefault   RouteType = "default"
	RouteTypeBlackhole RouteType = "blackhole"
)

// IPRouteComponent wraps an IP route and implements the NetworkingComponent interface.
// IP routes define the gateway/interface for packets in custom routing tables.
type IPRouteComponent struct {
	ComponentBase
	route         *IPRoute
	routeType     RouteType
	interfaceName string // Empty for blackhole routes
	selector      *InterfaceSelector
	cfg           *config.IPSetConfig
}

// NewDefaultRouteComponent creates a new default route component for a specific interface
func NewDefaultRouteComponent(cfg *config.IPSetConfig, iface *Interface, selector *InterfaceSelector) *IPRouteComponent {
	// Find the index of this interface in the ipset's interface list
	ifaceIndex := -1
	ifaceName := iface.Attrs().Name
	for idx, name := range cfg.Routing.Interfaces {
		if name == ifaceName {
			ifaceIndex = idx
			break
		}
	}
	// If interface not found in config, use 0 as default
	if ifaceIndex == -1 {
		ifaceIndex = 0
	}

	route := BuildDefaultRoute(cfg.IPVersion, *iface, cfg.Routing.IPRouteTable, ifaceIndex)
	return &IPRouteComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPRoute,
			description:   route.String(),
		},
		route:         route,
		routeType:     RouteTypeDefault,
		interfaceName: iface.Attrs().Name,
		selector:      selector,
		cfg:           cfg,
	}
}

// NewBlackholeRouteComponent creates a new blackhole route component
func NewBlackholeRouteComponent(cfg *config.IPSetConfig, selector *InterfaceSelector) *IPRouteComponent {
	route := BuildBlackholeRoute(cfg.IPVersion, cfg.Routing.IPRouteTable)
	return &IPRouteComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPRoute,
			description:   route.String(),
		},
		route:     route,
		routeType: RouteTypeBlackhole,
		selector:  selector,
		cfg:       cfg,
	}
}

// NewDefaultRouteComponentFromName creates a route component from just the interface name.
// This is useful when we don't have the Interface object yet.
func NewDefaultRouteComponentFromName(cfg *config.IPSetConfig, interfaceName string, selector *InterfaceSelector) (*IPRouteComponent, error) {
	iface, err := GetInterface(interfaceName)
	if err != nil {
		return nil, fmt.Errorf("failed to get interface %s: %w", interfaceName, err)
	}
	return NewDefaultRouteComponent(cfg, iface, selector), nil
}

// IsExists checks if the IP route currently exists in the system
func (c *IPRouteComponent) IsExists() (bool, error) {
	return c.route.IsExists()
}

// ShouldExist determines if this route should be present.
//
// For default routes:
//   - Should exist only if this interface is the best available interface
//
// For blackhole routes:
//   - Should exist if kill-switch is enabled (true/default)
//   - Should NOT exist if kill-switch is disabled (false)
//   - Kill-switch controls whether traffic is blocked (blackhole) or leaked (no route)
func (c *IPRouteComponent) ShouldExist() bool {
	switch c.routeType {
	case RouteTypeDefault:
		// Check current interface availability
		bestIface, _, err := c.selector.ChooseBest(c.cfg)
		if err != nil {
			// If we can't determine best interface, be conservative
			return false
		}
		// Default route should exist if this is the best interface
		if bestIface != nil && bestIface.Attrs().Name == c.interfaceName {
			return true
		}
		return false
	case RouteTypeBlackhole:
		// Blackhole route should exist based on kill-switch setting
		// If enabled (true/default): block traffic with blackhole route
		// If disabled (false): allow traffic to leak (no blackhole route)
		return c.cfg.Routing.KillSwitch
	default:
		return false
	}
}

// CreateIfNotExists creates the IP route if it doesn't already exist
func (c *IPRouteComponent) CreateIfNotExists() error {
	_, err := c.route.AddIfNotExists()
	return err
}

// DeleteIfExists removes the IP route if it exists
func (c *IPRouteComponent) DeleteIfExists() error {
	_, err := c.route.DelIfExists()
	return err
}

// GetRoute returns the underlying IP route
func (c *IPRouteComponent) GetRoute() *IPRoute {
	return c.route
}

// GetInterfaceName returns the interface name for default routes (empty for blackhole)
func (c *IPRouteComponent) GetInterfaceName() string {
	return c.interfaceName
}

// GetRouteType returns the type of this route
func (c *IPRouteComponent) GetRouteType() RouteType {
	return c.routeType
}
