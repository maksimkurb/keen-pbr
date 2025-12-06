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
