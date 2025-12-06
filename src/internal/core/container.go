package core

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
//	deps := core.NewAppDependencies(core.AppConfig{
//	    KeeneticURL: "http://192.168.1.1/rci",
//	})
//	networkMgr := deps.NetworkManager()
type AppDependencies struct {
	// Router client
	keeneticClient KeeneticClient

	// Domain managers
	networkManager NetworkManager
	ipsetManager   *networking.IPSetManagerImpl
	listManager    *lists.Manager
}

// AppConfig holds configuration for creating application dependencies.
type AppConfig struct {
	// KeeneticURL is the base URL for the Keenetic RCI API.
	// If empty, defaults to "http://127.0.0.1:79/rci" (local RCI endpoint).
	// Common values:
	//   - "http://127.0.0.1:79/rci" - local RCI endpoint (default)
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
	networkManager := networking.NewManager(keeneticClient)
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
//   - Keenetic API at http://127.0.0.1:79/rci (local RCI endpoint)
//   - Real networking implementations
func NewDefaultDependencies() *AppDependencies {
	return NewAppDependencies(AppConfig{})
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
func (d *AppDependencies) IPSetManager() *networking.IPSetManagerImpl {
	return d.ipsetManager
}

// ListManager returns the list manager with caching.
func (d *AppDependencies) ListManager() *lists.Manager {
	return d.listManager
}
