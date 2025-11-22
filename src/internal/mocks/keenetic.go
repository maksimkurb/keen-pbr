// Package mocks provides mock implementations for testing.
//
// This package should ONLY be imported in test files (_test.go).
// The Go toolchain will automatically exclude this package from production builds
// since it's not imported in any production code.
package mocks

import (
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// MockKeeneticClient is a mock implementation of the KeeneticClient interface.
//
// It allows tests to provide custom behavior for each method through function fields.
// If a function field is nil, a sensible default implementation is used.
//
// Example usage:
//
//	mock := &MockKeeneticClient{
//	    GetVersionFunc: func() (*keenetic.KeeneticVersion, error) {
//	        return &keenetic.KeeneticVersion{Major: 4, Minor: 3}, nil
//	    },
//	}
//	interfaces, err := mock.GetInterfaces()
type MockKeeneticClient struct {
	// GetVersionFunc is called by GetVersion if not nil
	GetVersionFunc func() (*keenetic.KeeneticVersion, error)

	// GetRawVersionFunc is called by GetRawVersion if not nil
	GetRawVersionFunc func() (string, error)

	// GetInterfacesFunc is called by GetInterfaces if not nil
	GetInterfacesFunc func() (map[string]keenetic.Interface, error)

	// GetDNSServersFunc is called by GetDNSServers if not nil
	GetDNSServersFunc func() ([]keenetic.DNSServerInfo, error)
}

// GetVersion returns the Keenetic OS version.
//
// If GetVersionFunc is set, it calls that function.
// Otherwise, returns a default version 4.3.
func (m *MockKeeneticClient) GetVersion() (*keenetic.KeeneticVersion, error) {
	if m.GetVersionFunc != nil {
		return m.GetVersionFunc()
	}
	// Default: return a modern version that supports all features
	return &keenetic.KeeneticVersion{Major: 4, Minor: 3}, nil
}

// GetRawVersion returns the raw Keenetic OS version string.
//
// If GetRawVersionFunc is set, it calls that function.
// Otherwise, returns a default version string "4.03.C.7.0-0".
func (m *MockKeeneticClient) GetRawVersion() (string, error) {
	if m.GetRawVersionFunc != nil {
		return m.GetRawVersionFunc()
	}
	// Default: return a mock version string
	return "4.03.C.7.0-0", nil
}

// GetInterfaces returns network interfaces from the Keenetic router.
//
// If GetInterfacesFunc is set, it calls that function.
// Otherwise, returns a default set of interfaces with one connected VPN interface.
func (m *MockKeeneticClient) GetInterfaces() (map[string]keenetic.Interface, error) {
	if m.GetInterfacesFunc != nil {
		return m.GetInterfacesFunc()
	}
	// Default: return a mock VPN interface that is connected
	return map[string]keenetic.Interface{
		"vpn0": {
			ID:          "Wireguard0",
			Type:        "Wireguard",
			Description: "Test VPN",
			Link:        keenetic.KeeneticLinkUp,
			Connected:   keenetic.KeeneticConnected,
			State:       "up",
			SystemName:  "vpn0",
		},
	}, nil
}

// GetDNSServers returns DNS servers configured on the router.
//
// If GetDNSServersFunc is set, it calls that function.
// Otherwise, returns an empty list.
func (m *MockKeeneticClient) GetDNSServers() ([]keenetic.DNSServerInfo, error) {
	if m.GetDNSServersFunc != nil {
		return m.GetDNSServersFunc()
	}
	// Default: return empty list
	return []keenetic.DNSServerInfo{}, nil
}

// NewMockKeeneticClient creates a new mock client with default behavior.
//
// This is a convenience constructor that returns a mock with sensible defaults.
// You can override individual methods by setting the function fields.
func NewMockKeeneticClient() *MockKeeneticClient {
	return &MockKeeneticClient{}
}

// NewMockKeeneticClientWithInterfaces creates a mock client that returns the specified interfaces.
//
// This is a convenience constructor for tests that need to control interface state.
func NewMockKeeneticClientWithInterfaces(interfaces map[string]keenetic.Interface) *MockKeeneticClient {
	return &MockKeeneticClient{
		GetInterfacesFunc: func() (map[string]keenetic.Interface, error) {
			return interfaces, nil
		},
	}
}

// NewMockKeeneticClientWithVersion creates a mock client that returns the specified version.
//
// This is a convenience constructor for tests that need to control version detection.
func NewMockKeeneticClientWithVersion(major, minor int) *MockKeeneticClient {
	return &MockKeeneticClient{
		GetVersionFunc: func() (*keenetic.KeeneticVersion, error) {
			return &keenetic.KeeneticVersion{Major: major, Minor: minor}, nil
		},
	}
}
