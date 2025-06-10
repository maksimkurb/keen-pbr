package networking

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/lib/config"
)

func TestBuildDefaultRoute(t *testing.T) {
	tests := []struct {
		name     string
		ipFamily config.IpFamily
		table    int
		expected string
	}{
		{
			name:     "IPv4 default route",
			ipFamily: config.Ipv4,
			table:    100,
			expected: "0.0.0.0/0",
		},
		{
			name:     "IPv6 default route", 
			ipFamily: config.Ipv6,
			table:    100,
			expected: "::/0",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			iface := Interface{&mockNetlinkLink{name: "eth0"}}
			route := BuildDefaultRoute(tt.ipFamily, iface, tt.table)

			if route == nil {
				t.Fatal("Expected route to be created")
			}

			if route.Table != tt.table {
				t.Errorf("Expected table %d, got %d", tt.table, route.Table)
			}

			if route.Dst.String() != tt.expected {
				t.Errorf("Expected destination %s, got %s", tt.expected, route.Dst.String())
			}
		})
	}
}

func TestBuildBlackholeRoute(t *testing.T) {
	tests := []struct {
		name     string
		ipFamily config.IpFamily
		table    int
		expected string
	}{
		{
			name:     "IPv4 blackhole route",
			ipFamily: config.Ipv4,
			table:    200,
			expected: "0.0.0.0/0",
		},
		{
			name:     "IPv6 blackhole route",
			ipFamily: config.Ipv6, 
			table:    200,
			expected: "::/0",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			route := BuildBlackholeRoute(tt.ipFamily, tt.table)

			if route == nil {
				t.Fatal("Expected route to be created")
			}

			if route.Table != tt.table {
				t.Errorf("Expected table %d, got %d", tt.table, route.Table)
			}

			if route.Type != RTN_BLACKHOLE {
				t.Errorf("Expected blackhole route type %d, got %d", RTN_BLACKHOLE, route.Type)
			}

			if route.Dst.String() != tt.expected {
				t.Errorf("Expected destination %s, got %s", tt.expected, route.Dst.String())
			}
		})
	}
}

func TestBuildDefaultRoute_EdgeCases(t *testing.T) {
	tests := []struct {
		name     string
		ipFamily config.IpFamily
		table    int
		expectNil bool
	}{
		{
			name:     "Zero table number",
			ipFamily: config.Ipv4,
			table:    0,
			expectNil: false, // Zero table should be valid
		},
		{
			name:     "Negative table number",
			ipFamily: config.Ipv4,
			table:    -1,
			expectNil: false, // Implementation might handle this
		},
		{
			name:     "Large table number",
			ipFamily: config.Ipv4,
			table:    65535,
			expectNil: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			iface := Interface{&mockNetlinkLink{name: "test"}}
			route := BuildDefaultRoute(tt.ipFamily, iface, tt.table)

			if tt.expectNil {
				if route != nil {
					t.Error("Expected nil route")
				}
			} else {
				if route == nil {
					t.Error("Expected route to be created")
				} else if route.Table != tt.table {
					t.Errorf("Expected table %d, got %d", tt.table, route.Table)
				}
			}
		})
	}
}

// Mock constants that should match the actual implementation
const RTN_BLACKHOLE = 6  // This should match the actual netlink constant