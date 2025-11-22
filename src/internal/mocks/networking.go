package mocks

import (
	"net/netip"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// MockNetworkManager is a mock implementation of the NetworkManager interface.
//
// This allows testing components that depend on network configuration without
// actually modifying iptables, ip rules, or ip routes.
type MockNetworkManager struct {
	// ApplyPersistentConfigFunc is called by ApplyPersistentConfig if not nil
	ApplyPersistentConfigFunc func(ipsets []*config.IPSetConfig) error

	// ApplyRoutingConfigFunc is called by ApplyRoutingConfig if not nil
	ApplyRoutingConfigFunc func(ipsets []*config.IPSetConfig) error

	// UpdateRoutingIfChangedFunc is called by UpdateRoutingIfChanged if not nil
	UpdateRoutingIfChangedFunc func(ipsets []*config.IPSetConfig) (int, error)

	// UndoConfigFunc is called by UndoConfig if not nil
	UndoConfigFunc func(ipsets []*config.IPSetConfig) error

	// Track calls for verification in tests
	ApplyPersistentConfigCalls  int
	ApplyRoutingConfigCalls     int
	UpdateRoutingIfChangedCalls int
	UndoConfigCalls             int
}

// ApplyPersistentConfig applies persistent network configuration.
func (m *MockNetworkManager) ApplyPersistentConfig(ipsets []*config.IPSetConfig) error {
	m.ApplyPersistentConfigCalls++
	if m.ApplyPersistentConfigFunc != nil {
		return m.ApplyPersistentConfigFunc(ipsets)
	}
	return nil
}

// ApplyRoutingConfig applies dynamic routing configuration.
func (m *MockNetworkManager) ApplyRoutingConfig(ipsets []*config.IPSetConfig) error {
	m.ApplyRoutingConfigCalls++
	if m.ApplyRoutingConfigFunc != nil {
		return m.ApplyRoutingConfigFunc(ipsets)
	}
	return nil
}

// UpdateRoutingIfChanged updates routing configuration only where interfaces changed.
func (m *MockNetworkManager) UpdateRoutingIfChanged(ipsets []*config.IPSetConfig) (int, error) {
	m.UpdateRoutingIfChangedCalls++
	if m.UpdateRoutingIfChangedFunc != nil {
		return m.UpdateRoutingIfChangedFunc(ipsets)
	}
	return 0, nil
}

// UndoConfig removes all network configuration.
func (m *MockNetworkManager) UndoConfig(ipsets []*config.IPSetConfig) error {
	m.UndoConfigCalls++
	if m.UndoConfigFunc != nil {
		return m.UndoConfigFunc(ipsets)
	}
	return nil
}

// NewMockNetworkManager creates a new mock network manager with default behavior.
func NewMockNetworkManager() *MockNetworkManager {
	return &MockNetworkManager{}
}

// MockRouteManager is a mock implementation of the RouteManager interface.
//
// This allows testing route management logic without modifying the actual routing table.
type MockRouteManager struct {
	// AddRouteFunc is called by AddRoute if not nil
	AddRouteFunc func(route *networking.IPRoute) error

	// DelRouteFunc is called by DelRoute if not nil
	DelRouteFunc func(route *networking.IPRoute) error

	// ListRoutesFunc is called by ListRoutes if not nil
	ListRoutesFunc func(table int) ([]*networking.IPRoute, error)

	// AddRouteIfNotExistsFunc is called by AddRouteIfNotExists if not nil
	AddRouteIfNotExistsFunc func(route *networking.IPRoute) error

	// DelRouteIfExistsFunc is called by DelRouteIfExists if not nil
	DelRouteIfExistsFunc func(route *networking.IPRoute) error

	// Track calls and routes for verification
	AddRouteCalls   int
	DelRouteCalls   int
	ListRoutesCalls int
	Routes          []*networking.IPRoute // Simulated route table
}

// AddRoute adds a route to the simulated routing table.
func (m *MockRouteManager) AddRoute(route *networking.IPRoute) error {
	m.AddRouteCalls++
	if m.AddRouteFunc != nil {
		return m.AddRouteFunc(route)
	}
	m.Routes = append(m.Routes, route)
	return nil
}

// DelRoute removes a route from the simulated routing table.
func (m *MockRouteManager) DelRoute(route *networking.IPRoute) error {
	m.DelRouteCalls++
	if m.DelRouteFunc != nil {
		return m.DelRouteFunc(route)
	}
	// Remove route from simulated table
	for i, r := range m.Routes {
		if r == route {
			m.Routes = append(m.Routes[:i], m.Routes[i+1:]...)
			break
		}
	}
	return nil
}

// ListRoutes returns routes from the simulated routing table.
func (m *MockRouteManager) ListRoutes(table int) ([]*networking.IPRoute, error) {
	m.ListRoutesCalls++
	if m.ListRoutesFunc != nil {
		return m.ListRoutesFunc(table)
	}
	// Filter routes by table
	var routes []*networking.IPRoute
	for _, r := range m.Routes {
		if r.Table == table {
			routes = append(routes, r)
		}
	}
	return routes, nil
}

// AddRouteIfNotExists adds a route only if it doesn't exist.
func (m *MockRouteManager) AddRouteIfNotExists(route *networking.IPRoute) error {
	if m.AddRouteIfNotExistsFunc != nil {
		return m.AddRouteIfNotExistsFunc(route)
	}
	// Check if route exists
	for _, r := range m.Routes {
		if r == route {
			return nil // Already exists
		}
	}
	return m.AddRoute(route)
}

// DelRouteIfExists removes a route only if it exists.
func (m *MockRouteManager) DelRouteIfExists(route *networking.IPRoute) error {
	if m.DelRouteIfExistsFunc != nil {
		return m.DelRouteIfExistsFunc(route)
	}
	return m.DelRoute(route)
}

// NewMockRouteManager creates a new mock route manager.
func NewMockRouteManager() *MockRouteManager {
	return &MockRouteManager{
		Routes: make([]*networking.IPRoute, 0),
	}
}

// MockInterfaceProvider is a mock implementation of the InterfaceProvider interface.
//
// This allows testing interface-related logic without actual network interfaces.
type MockInterfaceProvider struct {
	// GetInterfaceFunc is called by GetInterface if not nil
	GetInterfaceFunc func(name string) (*networking.Interface, error)

	// GetInterfaceListFunc is called by GetInterfaceList if not nil
	GetInterfaceListFunc func() ([]networking.Interface, error)

	// Simulated interfaces
	Interfaces map[string]*networking.Interface
}

// GetInterface retrieves a specific interface by name.
func (m *MockInterfaceProvider) GetInterface(name string) (*networking.Interface, error) {
	if m.GetInterfaceFunc != nil {
		return m.GetInterfaceFunc(name)
	}
	if iface, ok := m.Interfaces[name]; ok {
		return iface, nil
	}
	return nil, nil
}

// GetInterfaceList retrieves all network interfaces.
func (m *MockInterfaceProvider) GetInterfaceList() ([]networking.Interface, error) {
	if m.GetInterfaceListFunc != nil {
		return m.GetInterfaceListFunc()
	}
	var interfaces []networking.Interface
	for _, iface := range m.Interfaces {
		interfaces = append(interfaces, *iface)
	}
	return interfaces, nil
}

// NewMockInterfaceProvider creates a new mock interface provider.
func NewMockInterfaceProvider() *MockInterfaceProvider {
	return &MockInterfaceProvider{
		Interfaces: make(map[string]*networking.Interface),
	}
}

// MockIPSetManager is a mock implementation of the IPSetManager interface.
//
// This allows testing ipset operations without actually creating ipsets.
type MockIPSetManager struct {
	// CreateFunc is called by Create if not nil
	CreateFunc func(name string, family config.IPFamily) error

	// FlushFunc is called by Flush if not nil
	FlushFunc func(name string) error

	// ImportFunc is called by Import if not nil
	ImportFunc func(config *config.IPSetConfig, networks []netip.Prefix) error

	// CreateIfAbsentFunc is called by CreateIfAbsent if not nil
	CreateIfAbsentFunc func(config *config.Config) error

	// Track calls and state
	CreateCalls         int
	FlushCalls          int
	ImportCalls         int
	CreateIfAbsentCalls int
	CreatedIPSets       map[string]config.IPFamily // name -> family
	ImportedNetworks    map[string][]netip.Prefix  // ipset name -> networks
}

// Create creates a new ipset.
func (m *MockIPSetManager) Create(name string, family config.IPFamily) error {
	m.CreateCalls++
	if m.CreateFunc != nil {
		return m.CreateFunc(name, family)
	}
	if m.CreatedIPSets == nil {
		m.CreatedIPSets = make(map[string]config.IPFamily)
	}
	m.CreatedIPSets[name] = family
	return nil
}

// Flush removes all entries from an ipset.
func (m *MockIPSetManager) Flush(name string) error {
	m.FlushCalls++
	if m.FlushFunc != nil {
		return m.FlushFunc(name)
	}
	if m.ImportedNetworks != nil {
		delete(m.ImportedNetworks, name)
	}
	return nil
}

// Import adds networks to an ipset.
func (m *MockIPSetManager) Import(cfg *config.IPSetConfig, networks []netip.Prefix) error {
	m.ImportCalls++
	if m.ImportFunc != nil {
		return m.ImportFunc(cfg, networks)
	}
	if m.ImportedNetworks == nil {
		m.ImportedNetworks = make(map[string][]netip.Prefix)
	}
	m.ImportedNetworks[cfg.IPSetName] = networks
	return nil
}

// CreateIfAbsent ensures all ipsets exist.
func (m *MockIPSetManager) CreateIfAbsent(cfg *config.Config) error {
	m.CreateIfAbsentCalls++
	if m.CreateIfAbsentFunc != nil {
		return m.CreateIfAbsentFunc(cfg)
	}
	for _, ipset := range cfg.IPSets {
		m.Create(ipset.IPSetName, ipset.IPVersion)
	}
	return nil
}

// NewMockIPSetManager creates a new mock ipset manager.
func NewMockIPSetManager() *MockIPSetManager {
	return &MockIPSetManager{
		CreatedIPSets:    make(map[string]config.IPFamily),
		ImportedNetworks: make(map[string][]netip.Prefix),
	}
}
