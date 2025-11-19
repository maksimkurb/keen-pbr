package networking

import (
	"fmt"
	"os/exec"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// IPSetComponent wraps an IPSet and implements the NetworkingComponent interface.
// IPSets store IP addresses/subnets for policy routing decisions.
type IPSetComponent struct {
	ComponentBase
	ipset *IPSet
}

// NewIPSetComponent creates a new IPSet component from configuration
func NewIPSetComponent(cfg *config.IPSetConfig) *IPSetComponent {
	ipset := BuildIPSet(cfg.IPSetName, cfg.IPVersion)
	return &IPSetComponent{
		ComponentBase: ComponentBase{
			ipsetName:     cfg.IPSetName,
			componentType: ComponentTypeIPSet,
			description:   "IPSet must exist to store IP addresses/subnets for policy routing",
		},
		ipset: ipset,
	}
}

// IsExists checks if the IPSet currently exists in the system
func (c *IPSetComponent) IsExists() (bool, error) {
	return c.ipset.IsExists()
}

// ShouldExist returns true because IPSets should always exist for configured rules
func (c *IPSetComponent) ShouldExist() bool {
	return true
}

// CreateIfNotExists creates the IPSet if it doesn't already exist
func (c *IPSetComponent) CreateIfNotExists() error {
	exists, err := c.IsExists()
	if err != nil {
		return fmt.Errorf("failed to check if ipset exists: %w", err)
	}
	if exists {
		return nil
	}
	return c.ipset.CreateIfNotExists()
}

// DeleteIfExists removes the IPSet if it exists
func (c *IPSetComponent) DeleteIfExists() error {
	exists, err := c.IsExists()
	if err != nil {
		return fmt.Errorf("failed to check if ipset exists: %w", err)
	}
	if !exists {
		return nil
	}

	// Destroy the ipset
	cmd := exec.Command("ipset", "destroy", c.ipset.Name)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to destroy ipset %s: %w", c.ipset.Name, err)
	}
	return nil
}

// GetCommand returns the CLI command for manual inspection
func (c *IPSetComponent) GetCommand() string {
	return fmt.Sprintf("ipset list %s", c.ipset.Name)
}

// GetIPSet returns the underlying IPSet for operations like populating
func (c *IPSetComponent) GetIPSet() *IPSet {
	return c.ipset
}
