package networking

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
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
	route         *IpRoute
	routeType     RouteType
	interfaceName string // Empty for blackhole routes
	selector      *InterfaceSelector
	cfg           *config.IPSetConfig
}

// NewDefaultRouteComponent creates a new default route component for a specific interface
func NewDefaultRouteComponent(cfg *config.IPSetConfig, iface *Interface, selector *InterfaceSelector) *IPRouteComponent {
	route := BuildDefaultRoute(cfg.IPVersion, *iface, cfg.Routing.IpRouteTable)
	return &IPRouteComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPRoute,
			description:   "IP routes define the gateway/interface for packets in the custom routing table",
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
	route := BuildBlackholeRoute(cfg.IPVersion, cfg.Routing.IpRouteTable)
	return &IPRouteComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPRoute,
			description:   "Blackhole route blocks traffic when all interfaces are down",
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

// ShouldExist determines if this route should be present based on interface availability.
//
// For default routes:
//   - Should exist only if this interface is the best available interface
//
// For blackhole routes:
//   - Should exist only when no interfaces are available
func (c *IPRouteComponent) ShouldExist() bool {
	// Check current interface availability
	bestIface, err := c.selector.ChooseBest(c.cfg)
	if err != nil {
		// If we can't determine best interface, be conservative
		return false
	}

	if c.routeType == RouteTypeDefault {
		// Default route should exist if this is the best interface
		if bestIface != nil && bestIface.Attrs().Name == c.interfaceName {
			return true
		}
		return false
	} else if c.routeType == RouteTypeBlackhole {
		// Blackhole should exist only when no interface is available
		return bestIface == nil
	}

	return false
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

// GetCommand returns the CLI command for manual execution
func (c *IPRouteComponent) GetCommand() string {
	if c.routeType == RouteTypeBlackhole {
		return fmt.Sprintf("ip route add blackhole default table %d", c.cfg.Routing.IpRouteTable)
	}
	// This is simplified; actual command depends on gateway
	return fmt.Sprintf("ip route add default dev %s table %d", c.interfaceName, c.cfg.Routing.IpRouteTable)
}

// GetRoute returns the underlying IP route
func (c *IPRouteComponent) GetRoute() *IpRoute {
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

// NewInterfaceSelectorFromDeps creates an InterfaceSelector from AppDependencies
func NewInterfaceSelectorFromDeps(deps domain.AppDependencies) *InterfaceSelector {
	keeneticClient := deps.KeeneticClient()
	// Convert domain.KeeneticClient to *keenetic.Client if it's the concrete type
	// This is a temporary bridge until we fully migrate to using domain interfaces
	if concreteClient, ok := keeneticClient.(*keenetic.Client); ok {
		return NewInterfaceSelector(concreteClient)
	}
	return NewInterfaceSelector(nil)
}
