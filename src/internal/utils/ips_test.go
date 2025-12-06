package utils

import (
	"net"
	"testing"
)

func TestIPv4ToNetmask_Success(t *testing.T) {
	tests := []struct {
		name     string
		ip       string
		mask     string
		expected string
	}{
		{
			name:     "Standard subnet",
			ip:       "192.168.1.1",
			mask:     "255.255.255.0",
			expected: "192.168.1.1/24",
		},
		{
			name:     "Class A",
			ip:       "10.0.0.1",
			mask:     "255.0.0.0",
			expected: "10.0.0.1/8",
		},
		{
			name:     "Class B",
			ip:       "172.16.1.1",
			mask:     "255.255.0.0",
			expected: "172.16.1.1/16",
		},
		{
			name:     "Single host",
			ip:       "203.0.113.1",
			mask:     "255.255.255.255",
			expected: "203.0.113.1/32",
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := IPv4ToNetmask(tt.ip, tt.mask)
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
			
			if result == nil {
				t.Fatal("Expected result to be non-nil")
			}
			
			resultStr := result.String()
			if resultStr != tt.expected {
				t.Errorf("Expected %s, got %s", tt.expected, resultStr)
			}
		})
	}
}

func TestIPv4ToNetmask_InvalidIP(t *testing.T) {
	tests := []struct {
		name string
		ip   string
		mask string
	}{
		{"Invalid IP", "invalid", "255.255.255.0"},
		{"IPv6 as IP", "2001:db8::1", "255.255.255.0"},
		{"Empty IP", "", "255.255.255.0"},
		{"Out of range IP", "256.1.1.1", "255.255.255.0"},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := IPv4ToNetmask(tt.ip, tt.mask)
			if err == nil {
				t.Error("Expected error for invalid IP")
			}
		})
	}
}

func TestIPv4ToNetmask_InvalidMask(t *testing.T) {
	tests := []struct {
		name string
		ip   string
		mask string
	}{
		{"Invalid mask", "192.168.1.1", "invalid"},
		{"IPv6 as mask", "192.168.1.1", "2001:db8::1"},
		{"Empty mask", "192.168.1.1", ""},
		{"Out of range mask", "192.168.1.1", "256.255.255.0"},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := IPv4ToNetmask(tt.ip, tt.mask)
			if err == nil {
				t.Error("Expected error for invalid mask")
			}
		})
	}
}

func TestIPv6ToNetmask_Success(t *testing.T) {
	tests := []struct {
		name      string
		ip        string
		prefixLen int
		expected  string
	}{
		{
			name:      "Standard prefix",
			ip:        "2001:db8::1",
			prefixLen: 64,
			expected:  "2001:db8::/64",
		},
		{
			name:      "Single host",
			ip:        "2001:db8::1",
			prefixLen: 128,
			expected:  "2001:db8::1/128",
		},
		{
			name:      "Large subnet",
			ip:        "2001:db8:1234:5678::1",
			prefixLen: 48,
			expected:  "2001:db8:1234::/48",
		},
		{
			name:      "Zero prefix",
			ip:        "2001:db8::1",
			prefixLen: 0,
			expected:  "::/0",
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := IPv6ToNetmask(tt.ip, tt.prefixLen)
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
			
			if result == nil {
				t.Fatal("Expected result to be non-nil")
			}
			
			resultStr := result.String()
			if resultStr != tt.expected {
				t.Errorf("Expected %s, got %s", tt.expected, resultStr)
			}
		})
	}
}

func TestIPv6ToNetmask_InvalidIP(t *testing.T) {
	tests := []struct {
		name      string
		ip        string
		prefixLen int
	}{
		{"Invalid IP", "invalid", 64},
		{"Empty IP", "", 64},
		{"Malformed IPv6", "2001:db8::g", 64},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := IPv6ToNetmask(tt.ip, tt.prefixLen)
			if err == nil {
				t.Error("Expected error for invalid IP")
			}
		})
	}
}

func TestIPv6ToNetmask_InvalidPrefixLen(t *testing.T) {
	tests := []struct {
		name      string
		ip        string
		prefixLen int
	}{
		{"Negative prefix", "2001:db8::1", -1},
		{"Too large prefix", "2001:db8::1", 129},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := IPv6ToNetmask(tt.ip, tt.prefixLen)
			if err == nil {
				t.Error("Expected error for invalid prefix length")
			}
		})
	}
}

func TestIPv4ToNetmask_NetworkMasking(t *testing.T) {
	// Test that the function correctly applies the mask to get the network address
	result, err := IPv4ToNetmask("192.168.1.100", "255.255.255.0")
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	
	// The IP should remain as provided (not masked to network address)
	expectedIP := net.ParseIP("192.168.1.100").To4()
	if !result.IP.Equal(expectedIP) {
		t.Errorf("Expected IP %v, got %v", expectedIP, result.IP)
	}
	
	// But the mask should be correctly set
	expectedMask := net.IPv4Mask(255, 255, 255, 0)
	if result.Mask.String() != expectedMask.String() {
		t.Errorf("Expected mask %v, got %v", expectedMask, result.Mask)
	}
}

func TestIPv6ToNetmask_NetworkMasking(t *testing.T) {
	// Test that the function correctly applies the mask to get the network address
	result, err := IPv6ToNetmask("2001:db8::abcd", 64)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	
	// The IP should be masked to the network address
	expectedNetwork := "2001:db8::/64"
	if result.String() != expectedNetwork {
		t.Errorf("Expected network %s, got %s", expectedNetwork, result.String())
	}
}