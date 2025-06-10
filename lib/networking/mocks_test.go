package networking

import (
	"net"

	"github.com/vishvananda/netlink"
)

// Mock types for testing

type mockNetlinkLink struct {
	name  string
	up    bool
	index int
}

func (m *mockNetlinkLink) Attrs() *netlink.LinkAttrs {
	flags := net.Flags(0)
	if m.up {
		flags |= net.FlagUp
	}
	return &netlink.LinkAttrs{
		Name:  m.name,
		Index: m.index,
		Flags: flags,
	}
}

func (m *mockNetlinkLink) Type() string { return "mock" }

// Helper functions

func stringPtr(s string) *string {
	return &s
}

func boolPtr(b bool) *bool {
	return &b
}

func contains(s, substr string) bool {
	return len(s) >= len(substr) && (s == substr || 
		len(s) > len(substr) && (s[:len(substr)] == substr || 
		s[len(s)-len(substr):] == substr || 
		containsRecursive(s[1:], substr)))
}

func containsRecursive(s, substr string) bool {
	if len(s) < len(substr) {
		return false
	}
	if s[:len(substr)] == substr {
		return true
	}
	return containsRecursive(s[1:], substr)
}