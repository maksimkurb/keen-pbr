package domain

import (
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// AppDependencies is a dependency injection container that holds all application dependencies.
//
// This container provides a centralized place to manage dependencies and enables:
//   - Easy testing with mock implementations
//   - Configuration-driven dependency creation
//   - Explicit dependency management instead of global state
//
// Usage:
//
//	deps := domain.NewAppDependencies(domain.AppConfig{
//	    KeeneticURL: "http://192.168.1.1/rci",
//	})
//	networkMgr := deps.NetworkManager()
type AppDependencies struct {
	// Core clients
	keeneticClient KeeneticClient

	// Domain managers
	networkManager NetworkManager
	ipsetManager   IPSetManager
	listManager    *lists.Manager
}

// AppConfig holds configuration for creating application dependencies.
type AppConfig struct {
	// KeeneticURL is the base URL for the Keenetic RCI API.
	// If empty, defaults to "http://localhost:79/rci" (local RCI endpoint).
	// Common values:
	//   - "http://localhost:79/rci" - local RCI endpoint (default)
	//   - "http://192.168.1.1/rci" - remote router at 192.168.1.1
	KeeneticURL string

	// DisableKeenetic disables Keenetic API integration entirely.
	// Useful for environments without a Keenetic router.
	DisableKeenetic bool
}

// NewAppDependencies creates a new dependency container with production implementations.
//
// This factory method creates real implementations of all interfaces using the
// provided configuration. For testing, use NewTestDependencies or inject mocks directly.
func NewAppDependencies(cfg AppConfig) *AppDependencies {
	// Create Keenetic client
	var keeneticClient KeeneticClient
	if cfg.DisableKeenetic {
		keeneticClient = nil
	} else {
		if cfg.KeeneticURL == "" {
			// Use default base URL
			keeneticClient = keenetic.NewClient(nil)
		} else {
			// Use custom base URL
			keeneticClient = keenetic.NewClientWithBaseURL(cfg.KeeneticURL, nil)
		}
	}

	// Create domain managers
	// Note: networking.NewManager expects *keenetic.Client, not the interface
	// This is safe because our factory always creates *keenetic.Client or nil
	var concreteClient *keenetic.Client
	if keeneticClient != nil {
		concreteClient = keeneticClient.(*keenetic.Client)
	}
	networkManager := networking.NewManager(concreteClient)
	ipsetManager := networking.NewIPSetManager()
	listManager := lists.NewManager()

	return &AppDependencies{
		keeneticClient: keeneticClient,
		networkManager: networkManager,
		ipsetManager:   ipsetManager,
		listManager:    listManager,
	}
}

// NewDefaultDependencies creates dependencies using default configuration.
//
// This is equivalent to NewAppDependencies(AppConfig{}) and uses:
//   - Keenetic API at http://localhost:79/rci (local RCI endpoint)
//   - Real networking implementations
func NewDefaultDependencies() *AppDependencies {
	return NewAppDependencies(AppConfig{})
}

// NewTestDependencies creates a dependency container with mock implementations.
//
// This is a convenience method for testing. Provide mock implementations for
// any dependencies you want to control in your tests.
func NewTestDependencies(
	keeneticClient KeeneticClient,
	networkManager NetworkManager,
	ipsetManager IPSetManager,
) *AppDependencies {
	return &AppDependencies{
		keeneticClient: keeneticClient,
		networkManager: networkManager,
		ipsetManager:   ipsetManager,
		listManager:    lists.NewManager(),
	}
}

// KeeneticClient returns the Keenetic API client.
func (d *AppDependencies) KeeneticClient() KeeneticClient {
	return d.keeneticClient
}

// NetworkManager returns the network configuration manager.
func (d *AppDependencies) NetworkManager() NetworkManager {
	return d.networkManager
}

// IPSetManager returns the ipset manager.
func (d *AppDependencies) IPSetManager() IPSetManager {
	return d.ipsetManager
}

// ListManager returns the list manager with caching.
func (d *AppDependencies) ListManager() *lists.Manager {
	return d.listManager
}
