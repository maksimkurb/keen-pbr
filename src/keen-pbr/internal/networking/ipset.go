package networking

import (
	"fmt"
	"os/exec"
	"strings"
)

// IPSet represents an ipset configuration
type IPSet struct {
	Name     string
	Type     string
	Family   string
	Elements []string
}

// NewIPSet creates a new ipset
func NewIPSet(name, setType, family string) *IPSet {
	return &IPSet{
		Name:     name,
		Type:     setType,
		Family:   family,
		Elements: []string{},
	}
}

// AddElement adds an element to the ipset
func (s *IPSet) AddElement(element string) {
	s.Elements = append(s.Elements, element)
}

// Create creates the ipset
func (s *IPSet) Create() error {
	// Check if ipset exists
	cmd := exec.Command("ipset", "list", s.Name)
	if err := cmd.Run(); err == nil {
		// IPSet already exists, flush it
		if err := s.Flush(); err != nil {
			return fmt.Errorf("failed to flush existing ipset %s: %w", s.Name, err)
		}
		return nil
	}

	// Create ipset
	args := []string{"create", s.Name, s.Type}
	if s.Family != "" {
		args = append(args, "family", s.Family)
	}

	cmd = exec.Command("ipset", args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to create ipset %s: %w, output: %s", s.Name, err, string(output))
	}

	return nil
}

// AddElements adds all elements to the ipset
func (s *IPSet) AddElements() error {
	for _, element := range s.Elements {
		cmd := exec.Command("ipset", "add", s.Name, element, "-exist")
		output, err := cmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("failed to add element %s to ipset %s: %w, output: %s",
				element, s.Name, err, string(output))
		}
	}
	return nil
}

// Flush removes all elements from the ipset
func (s *IPSet) Flush() error {
	cmd := exec.Command("ipset", "flush", s.Name)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to flush ipset %s: %w, output: %s", s.Name, err, string(output))
	}
	return nil
}

// Destroy destroys the ipset
func (s *IPSet) Destroy() error {
	cmd := exec.Command("ipset", "destroy", s.Name)
	output, err := cmd.CombinedOutput()
	if err != nil {
		// Ignore error if ipset doesn't exist
		if strings.Contains(string(output), "does not exist") {
			return nil
		}
		return fmt.Errorf("failed to destroy ipset %s: %w, output: %s", s.Name, err, string(output))
	}
	return nil
}

// Exists checks if ipset exists
func (s *IPSet) Exists() bool {
	cmd := exec.Command("ipset", "list", s.Name)
	return cmd.Run() == nil
}
