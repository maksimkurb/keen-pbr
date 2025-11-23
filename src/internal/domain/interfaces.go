// Package domain defines core interfaces for dependency injection and abstraction.
//
// This package contains the fundamental interfaces that enable loose coupling between
// components and facilitate testing through dependency injection.
package domain

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// KeeneticClient defines the interface for interacting with the Keenetic Router RCI API.
//
// This interface abstracts the Keenetic API client, allowing for easy mocking in tests
// and potential future implementations that don't rely on HTTP.
type KeeneticClient interface {
	// GetVersion retrieves the Keenetic OS version information.
	GetVersion() (*keenetic.KeeneticVersion, error)

	// GetRawVersion retrieves the raw Keenetic OS version string.
	GetRawVersion() (string, error)

	// GetInterfaces retrieves all network interfaces from the Keenetic router,
	// mapped by their system names (Linux interface names).
	GetInterfaces() (map[string]keenetic.Interface, error)

	// GetDNSServers retrieves the list of DNS servers configured on the router.
	GetDNSServers() ([]keenetic.DNSServerInfo, error)
}

// NetworkManager defines the interface for managing network configuration.
//
// This interface handles the application, update, and removal of network routing
// configurations including iptables rules, ip rules, and ip routes.
type NetworkManager interface {
	// ApplyPersistentConfig applies persistent network configuration (iptables rules and ip rules)
	// that should remain active regardless of interface state.
	ApplyPersistentConfig(ipsets []*config.IPSetConfig) error

	// ApplyRoutingConfig updates dynamic routing configuration (ip routes)
	// based on the current interface states.
	ApplyRoutingConfig(ipsets []*config.IPSetConfig) error

	// UpdateRoutingIfChanged updates routing configuration only for ipsets where
	// the best interface has changed. This is more efficient than ApplyRoutingConfig
	// for monitoring scenarios. Returns the number of ipsets that were updated.
	UpdateRoutingIfChanged(ipsets []*config.IPSetConfig) (int, error)

	// UndoConfig removes all network configuration for the specified ipsets,
	// including iptables rules, ip rules, and ip routes.
	UndoConfig(ipsets []*config.IPSetConfig) error
}

