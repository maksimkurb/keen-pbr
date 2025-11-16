package mocks

import (
	"errors"
	"net/netip"
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/vishvananda/netlink"
)

// TestMockNetworkManager_DefaultBehavior tests default mock behavior
func TestMockNetworkManager_DefaultBehavior(t *testing.T) {
	mock := NewMockNetworkManager()

	ipsets := []*config.IPSetConfig{
		{IPSetName: "test-ipset"},
	}

	// Test ApplyPersistentConfig
	err := mock.ApplyPersistentConfig(ipsets)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.ApplyPersistentConfigCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.ApplyPersistentConfigCalls)
	}

	// Test ApplyRoutingConfig
	err = mock.ApplyRoutingConfig(ipsets)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.ApplyRoutingConfigCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.ApplyRoutingConfigCalls)
	}

	// Test UndoConfig
	err = mock.UndoConfig(ipsets)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.UndoConfigCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.UndoConfigCalls)
	}
}

// TestMockNetworkManager_CustomBehavior tests custom function behavior
func TestMockNetworkManager_CustomBehavior(t *testing.T) {
	expectedErr := errors.New("test error")

	mock := &MockNetworkManager{
		ApplyPersistentConfigFunc: func(ipsets []*config.IPSetConfig) error {
			return expectedErr
		},
	}

	ipsets := []*config.IPSetConfig{{IPSetName: "test"}}

	err := mock.ApplyPersistentConfig(ipsets)
	if err != expectedErr {
		t.Errorf("Expected custom error, got: %v", err)
	}
}

// TestMockRouteManager_DefaultBehavior tests default route manager behavior
func TestMockRouteManager_DefaultBehavior(t *testing.T) {
	mock := NewMockRouteManager()

	route1 := &networking.IpRoute{Route: &netlink.Route{Table: 100}}
	route2 := &networking.IpRoute{Route: &netlink.Route{Table: 100}}
	route3 := &networking.IpRoute{Route: &netlink.Route{Table: 200}}

	// Test AddRoute
	err := mock.AddRoute(route1)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.AddRouteCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.AddRouteCalls)
	}
	if len(mock.Routes) != 1 {
		t.Errorf("Expected 1 route, got %d", len(mock.Routes))
	}

	// Add more routes
	mock.AddRoute(route2)
	mock.AddRoute(route3)

	// Test ListRoutes
	routes, err := mock.ListRoutes(100)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(routes) != 2 {
		t.Errorf("Expected 2 routes in table 100, got %d", len(routes))
	}

	routes, err = mock.ListRoutes(200)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(routes) != 1 {
		t.Errorf("Expected 1 route in table 200, got %d", len(routes))
	}

	// Test DelRoute
	err = mock.DelRoute(route1)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(mock.Routes) != 2 {
		t.Errorf("Expected 2 routes after deletion, got %d", len(mock.Routes))
	}
}

// TestMockRouteManager_AddRouteIfNotExists tests idempotent route addition
func TestMockRouteManager_AddRouteIfNotExists(t *testing.T) {
	mock := NewMockRouteManager()

	route := &networking.IpRoute{Route: &netlink.Route{Table: 100}}

	// Add route first time
	err := mock.AddRouteIfNotExists(route)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(mock.Routes) != 1 {
		t.Errorf("Expected 1 route, got %d", len(mock.Routes))
	}

	// Add same route again - should not duplicate
	err = mock.AddRouteIfNotExists(route)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(mock.Routes) != 1 {
		t.Errorf("Expected still 1 route, got %d", len(mock.Routes))
	}
}

// TestMockInterfaceProvider_DefaultBehavior tests interface provider
func TestMockInterfaceProvider_DefaultBehavior(t *testing.T) {
	mock := NewMockInterfaceProvider()

	// Add mock interfaces
	mock.Interfaces["eth0"] = &networking.Interface{}
	mock.Interfaces["wlan0"] = &networking.Interface{}

	// Test GetInterface
	iface, err := mock.GetInterface("eth0")
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if iface == nil {
		t.Error("Expected interface, got nil")
	}

	// Test GetInterface for non-existent interface
	iface, err = mock.GetInterface("nonexistent")
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if iface != nil {
		t.Error("Expected nil for non-existent interface")
	}

	// Test GetInterfaceList
	interfaces, err := mock.GetInterfaceList()
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if len(interfaces) != 2 {
		t.Errorf("Expected 2 interfaces, got %d", len(interfaces))
	}
}

// TestMockIPSetManager_DefaultBehavior tests ipset manager
func TestMockIPSetManager_DefaultBehavior(t *testing.T) {
	mock := NewMockIPSetManager()

	// Test Create
	err := mock.Create("test-ipset", config.Ipv4)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.CreateCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.CreateCalls)
	}
	if len(mock.CreatedIPSets) != 1 {
		t.Errorf("Expected 1 ipset, got %d", len(mock.CreatedIPSets))
	}
	if family, ok := mock.CreatedIPSets["test-ipset"]; !ok || family != config.Ipv4 {
		t.Error("Expected test-ipset with IPv4 family")
	}

	// Test Import
	networks := []netip.Prefix{
		netip.MustParsePrefix("10.0.0.0/8"),
		netip.MustParsePrefix("192.168.0.0/16"),
	}
	cfg := &config.IPSetConfig{IPSetName: "test-ipset"}
	err = mock.Import(cfg, networks)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.ImportCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.ImportCalls)
	}
	if len(mock.ImportedNetworks["test-ipset"]) != 2 {
		t.Errorf("Expected 2 networks, got %d", len(mock.ImportedNetworks["test-ipset"]))
	}

	// Test Flush
	err = mock.Flush("test-ipset")
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.FlushCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.FlushCalls)
	}
	if _, exists := mock.ImportedNetworks["test-ipset"]; exists {
		t.Error("Expected networks to be removed after flush")
	}
}

// TestMockIPSetManager_CreateIfAbsent tests batch ipset creation
func TestMockIPSetManager_CreateIfAbsent(t *testing.T) {
	mock := NewMockIPSetManager()

	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{IPSetName: "ipset1", IPVersion: config.Ipv4},
			{IPSetName: "ipset2", IPVersion: config.Ipv6},
		},
	}

	err := mock.CreateIfAbsent(cfg)
	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}
	if mock.CreateIfAbsentCalls != 1 {
		t.Errorf("Expected 1 call, got %d", mock.CreateIfAbsentCalls)
	}
	if len(mock.CreatedIPSets) != 2 {
		t.Errorf("Expected 2 ipsets, got %d", len(mock.CreatedIPSets))
	}
}

// TestMockIntegration tests mocks working together
func TestMockIntegration(t *testing.T) {
	networkMgr := NewMockNetworkManager()
	ipsetMgr := NewMockIPSetManager()
	routeMgr := NewMockRouteManager()

	// Simulate a complete workflow
	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "test",
				IPVersion: config.Ipv4,
				Routing: &config.RoutingConfig{
					IpRouteTable: 100,
				},
			},
		},
	}

	// Create ipsets
	err := ipsetMgr.CreateIfAbsent(cfg)
	if err != nil {
		t.Fatalf("Failed to create ipsets: %v", err)
	}

	// Import networks
	networks := []netip.Prefix{netip.MustParsePrefix("10.0.0.0/8")}
	err = ipsetMgr.Import(cfg.IPSets[0], networks)
	if err != nil {
		t.Fatalf("Failed to import networks: %v", err)
	}

	// Apply network configuration
	err = networkMgr.ApplyPersistentConfig(cfg.IPSets)
	if err != nil {
		t.Fatalf("Failed to apply persistent config: %v", err)
	}

	err = networkMgr.ApplyRoutingConfig(cfg.IPSets)
	if err != nil {
		t.Fatalf("Failed to apply routing config: %v", err)
	}

	// Add routes
	route := &networking.IpRoute{Route: &netlink.Route{Table: 100}}
	err = routeMgr.AddRoute(route)
	if err != nil {
		t.Fatalf("Failed to add route: %v", err)
	}

	// Verify state
	if ipsetMgr.CreateIfAbsentCalls != 1 {
		t.Error("Expected CreateIfAbsent to be called")
	}
	if ipsetMgr.ImportCalls != 1 {
		t.Error("Expected Import to be called")
	}
	if networkMgr.ApplyPersistentConfigCalls != 1 {
		t.Error("Expected ApplyPersistentConfig to be called")
	}
	if networkMgr.ApplyRoutingConfigCalls != 1 {
		t.Error("Expected ApplyRoutingConfig to be called")
	}
	if len(routeMgr.Routes) != 1 {
		t.Errorf("Expected 1 route, got %d", len(routeMgr.Routes))
	}
}
