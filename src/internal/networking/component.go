package networking

// ComponentType identifies the type of networking component
type ComponentType string

const (
	ComponentTypeIPSet    ComponentType = "ipset"
	ComponentTypeIPRule   ComponentType = "ip_rule"
	ComponentTypeIPRoute  ComponentType = "ip_route"
	ComponentTypeIPTables ComponentType = "iptables"
)

// NetworkingComponent represents any network configuration element.
// This abstraction unifies apply and self-check logic by providing
// a consistent interface for all networking primitives.
type NetworkingComponent interface {
	// IsExists checks if the component currently exists in the system
	IsExists() (bool, error)

	// ShouldExist determines if this component should be present
	// based on current system state (e.g., interface availability)
	ShouldExist() bool

	// CreateIfNotExists creates the component if it doesn't exist
	CreateIfNotExists() error

	// DeleteIfExists removes the component if it exists
	DeleteIfExists() error

	// GetType returns the component type for categorization
	GetType() ComponentType

	// GetIPSetName returns the associated IPSet name (for grouping)
	GetIPSetName() string

	// GetDescription returns human-readable description
	GetDescription() string

	// GetCommand returns the CLI command for manual execution (debugging)
	GetCommand() string
}
