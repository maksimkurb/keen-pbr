package utils

import (
	"testing"
)

func TestIsDNSName_ValidDomains(t *testing.T) {
	validDomains := []string{
		"example.com",
		"sub.example.com",
		"a.b.c.d.example.com",
		"test-domain.org",
		"domain_with_underscore.net",
		"123domain.com",
		"domain123.info",
		"a.co",
		"x.y",
		"single",
		"test.",
		"test_",
	}
	
	for _, domain := range validDomains {
		t.Run(domain, func(t *testing.T) {
			if !IsDNSName(domain) {
				t.Errorf("Expected '%s' to be a valid DNS name", domain)
			}
		})
	}
}

func TestIsDNSName_InvalidDomains(t *testing.T) {
	invalidDomains := []string{
		"",                    // Empty string
		"192.168.1.1",        // IPv4 address
		"2001:db8::1",        // IPv6 address
		"127.0.0.1",          // Another IPv4
		"::1",                // IPv6 localhost
		"domain..com",        // Double dot
		".example.com",       // Leading dot
		"-domain.com",        // Leading hyphen in label
		"very-long-domain-name-that-exceeds-the-maximum-allowed-length-for-dns-names-which-is-253-characters-according-to-rfc-specifications-and-this-string-is-definitely-longer-than-that-limit-so-it-should-be-considered-invalid", // Too long
	}
	
	for _, domain := range invalidDomains {
		t.Run(domain, func(t *testing.T) {
			if IsDNSName(domain) {
				t.Errorf("Expected '%s' to be an invalid DNS name", domain)
			}
		})
	}
}

func TestIsDNSName_EdgeCases(t *testing.T) {
	tests := []struct {
		name     string
		domain   string
		expected bool
	}{
		{
			name:     "Single character domain",
			domain:   "a",
			expected: true,
		},
		{
			name:     "Single character with dot",
			domain:   "a.",
			expected: true,
		},
		{
			name:     "Maximum label length (63 chars)",
			domain:   "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk.com",
			expected: true,
		},
		{
			name:     "Label too long (64 chars)",
			domain:   "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl.com",
			expected: false,
		},
		{
			name:     "Long valid domain",
			domain:   "very.long.domain.name.with.many.labels.that.should.still.be.valid.example.com",
			expected: true,
		},
		{
			name:     "Numbers only",
			domain:   "123.456",
			expected: true,
		},
		{
			name:     "Mixed case",
			domain:   "ExAmPlE.CoM",
			expected: true,
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := IsDNSName(tt.domain)
			if result != tt.expected {
				t.Errorf("IsDNSName('%s') = %v, expected %v", tt.domain, result, tt.expected)
			}
		})
	}
}

func TestIsIP_ValidIPs(t *testing.T) {
	validIPs := []string{
		"192.168.1.1",
		"127.0.0.1",
		"0.0.0.0",
		"255.255.255.255",
		"203.0.113.1",
		"2001:db8::1",
		"::1",
		"2001:0db8:85a3:0000:0000:8a2e:0370:7334",
		"2001:db8:85a3::8a2e:370:7334",
		"::ffff:192.168.1.1",
		"::",
		"ff02::1",
	}
	
	for _, ip := range validIPs {
		t.Run(ip, func(t *testing.T) {
			if !IsIP(ip) {
				t.Errorf("Expected '%s' to be a valid IP address", ip)
			}
		})
	}
}

func TestIsIP_InvalidIPs(t *testing.T) {
	invalidIPs := []string{
		"",
		"example.com",
		"256.1.1.1",
		"192.168.1",
		"192.168.1.1.1",
		"192.168.1.256",
		"::g",
		"2001:db8::1::1",
		"not.an.ip",
		"192.168.1.-1",
		"hello world",
		"192.168.1.1x",
	}
	
	for _, ip := range invalidIPs {
		t.Run(ip, func(t *testing.T) {
			if IsIP(ip) {
				t.Errorf("Expected '%s' to be an invalid IP address", ip)
			}
		})
	}
}

func TestIsDNSName_VsIsIP(t *testing.T) {
	// Test that IP addresses are correctly identified and excluded from DNS names
	testCases := []string{
		"192.168.1.1",
		"127.0.0.1",
		"2001:db8::1",
		"::1",
	}
	
	for _, testCase := range testCases {
		t.Run(testCase, func(t *testing.T) {
			if !IsIP(testCase) {
				t.Errorf("Expected '%s' to be identified as IP", testCase)
			}
			
			if IsDNSName(testCase) {
				t.Errorf("Expected '%s' not to be identified as DNS name since it's an IP", testCase)
			}
		})
	}
}

