// Package domain defines core interfaces for dependency injection and abstraction.
//
// This package contains the fundamental interfaces that enable loose coupling between
// components and facilitate testing through dependency injection.
package domain

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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
// configurations including iptables rules, ip rules, ip routes, and global components
// like DNS redirect.
type NetworkManager interface {
	// SetGlobalConfig sets the global configuration for service-level components
	// like DNS redirect. This should be called before ApplyNetfilter.
	SetGlobalConfig(globalCfg networking.GlobalConfig)

	// GetGlobalConfig returns the current global configuration.
	GetGlobalConfig() networking.GlobalConfig

	// ApplyRouting applies routing configuration (ip rules and ip routes) for all ipsets.
	// When force=false: only updates routes if interfaces changed (efficient for periodic monitoring)
	// When force=true: unconditionally applies all routing (for reload/startup)
	// Returns the number of ipsets that were updated.
	ApplyRouting(ipsets []*config.IPSetConfig, force bool) (int, error)

	// ApplyNetfilter applies netfilter configuration (iptables rules and global components)
	// for all ipsets. This includes iptables rules for packet marking and DNS redirect rules.
	ApplyNetfilter(ipsets []*config.IPSetConfig) error

	// UndoConfig removes all network configuration for the specified ipsets,
	// including iptables rules, ip rules, ip routes, and global components.
	UndoConfig(ipsets []*config.IPSetConfig) error
}
