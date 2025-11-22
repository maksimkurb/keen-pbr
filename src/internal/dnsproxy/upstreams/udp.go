package upstreams

import (
	"context"
	"fmt"
	"net"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/miekg/dns"
)

const (
	// DNS protocol defaults
	defaultDNSPort = "53"

	// Timeout configuration
	udpClientTimeout = 3 * time.Second // Shorter than context timeout to avoid races
)

// UDPUpstream implements Upstream using plain UDP DNS.
type UDPUpstream struct {
	BaseUpstream
	address string
	client  *dns.Client
}

// NewUDPUpstream creates a new UDP DNS upstream.
// The domain parameter restricts the upstream to a specific domain (empty = all domains).
func NewUDPUpstream(address string, restrictedDomain string) (*UDPUpstream, error) {
	host := address
	if !containsPort(host) {
		host = net.JoinHostPort(host, defaultDNSPort)
	}

	// Validate address
	if _, _, err := net.SplitHostPort(host); err != nil {
		return nil, fmt.Errorf("invalid UDP address: %w", err)
	}

	return &UDPUpstream{
		BaseUpstream: BaseUpstream{Domain: restrictedDomain},
		address:      host,
		client: &dns.Client{
			Net:     "udp",
			Timeout: udpClientTimeout,
		},
	}, nil
}

// Query sends a DNS query to the UDP upstream.
func (u *UDPUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	resp, _, err := u.client.ExchangeContext(ctx, req, u.address)
	if err != nil {
		return nil, fmt.Errorf("UDP query failed: %w", err)
	}
	return resp, nil
}

// String returns a human-readable representation of the upstream.
func (u *UDPUpstream) String() string {
	if u.Domain != "" {
		return fmt.Sprintf("udp://%s (domain: %s)", u.address, u.Domain)
	}
	return fmt.Sprintf("udp://%s", u.address)
}

// Close closes any resources held by the upstream.
func (u *UDPUpstream) Close() error {
	return nil
}

// GetDNSServers returns the DNS server info for this upstream.
func (u *UDPUpstream) GetDNSServers() []keenetic.DNSServerInfo {
	var domain *string
	if u.Domain != "" {
		domain = &u.Domain
	}
	return []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    u.address,
			Endpoint: u.address,
			Domain:   domain,
		},
	}
}

// containsPort checks if the address contains a port number.
func containsPort(address string) bool {
	// For IPv6 addresses like [::1]:53, check after the closing bracket
	if idx := lastIndex(address, ']'); idx != -1 {
		return len(address) > idx+1 && address[idx+1] == ':'
	}
	// For IPv4 addresses, check for colon
	return lastIndex(address, ':') != -1
}

// lastIndex returns the index of the last occurrence of char in s, or -1 if not found.
func lastIndex(s string, char byte) int {
	for i := len(s) - 1; i >= 0; i-- {
		if s[i] == char {
			return i
		}
	}
	return -1
}
