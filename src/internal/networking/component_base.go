package networking

// ComponentBase provides common fields for all networking components.
// This struct is embedded in all concrete component implementations
// to provide basic metadata functionality.
//
// Embedding Pattern:
//
//	type IPSetComponent struct {
//	    ComponentBase  // Provides GetIPSetName, GetType, GetDescription
//	    ipset *IPSet   // Specific implementation details
//	}
//
// This approach reduces code duplication while allowing each component
// type to implement its own IsExists, ShouldExist, CreateIfNotExists,
// DeleteIfExists, and GetCommand methods.
type ComponentBase struct {
	ipsetName     string
	componentType ComponentType
	description   string
}

// GetIPSetName returns the associated IPSet name for grouping components
func (c *ComponentBase) GetIPSetName() string {
	return c.ipsetName
}

// GetType returns the component type for categorization and filtering
func (c *ComponentBase) GetType() ComponentType {
	return c.componentType
}

// GetDescription returns a human-readable description explaining what
// this component does and why it's needed
func (c *ComponentBase) GetDescription() string {
	return c.description
}
