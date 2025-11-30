package components

// Component represents a service component with lifecycle management
type Component interface {
	// Start starts the component
	Start() error

	// Stop stops the component
	Stop() error

	// Name returns the component name for logging
	Name() string
}

// ServiceManager interface defines the methods needed from commands.ServiceManager
// This interface is defined here to avoid import cycles
type ServiceManager interface {
	Start() error
	Stop() error
	IsRunning() bool
	RefreshRouting() error
	RefreshFirewall() error
	DownloadLists() error
	SetOnListsUpdated(callback func())
}
