package upstreams

import (
	"context"
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
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
		BaseUpstream: NewBaseUpstream(restrictedDomain),
		address:      host,
		client: &dns.Client{
			Net:     "udp",
			Timeout: udpClientTimeout,
		},
	}, nil
}

// Query sends a DNS query to the UDP upstream.
func (u *UDPUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	// Extract query info for logging
	queryInfo := "unknown"
	if len(req.Question) > 0 {
		q := req.Question[0]
		queryInfo = fmt.Sprintf("%s %s", q.Name, dns.TypeToString[q.Qtype])
	}

	upstreamStr := u.GetDNSStrings()[0]
	log.Debugf("[%04x] Querying upstream: %s for %s", req.Id, upstreamStr, queryInfo)

	resp, _, err := u.client.ExchangeContext(ctx, req, u.address)
	if err != nil {
		// Check if it's a context timeout vs network timeout
		if ctx.Err() == context.DeadlineExceeded {
			log.Warnf("[%04x] Upstream timeout (context) for query: %s (upstream: %s)", req.Id, queryInfo, upstreamStr)
		} else {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				log.Debugf("[%04x] Upstream timeout (network) for query: %s (upstream: %s)", req.Id, queryInfo, upstreamStr)
			} else {
				log.Debugf("[%04x] Upstream error for query %s (upstream: %s): %v", req.Id, queryInfo, upstreamStr, err)
			}
		}
		return nil, err
	}
	return resp, nil
}

// Close closes any resources held by the upstream.
func (u *UDPUpstream) Close() error {
	return nil
}

// GetDNSStrings returns an array of DNS server strings in URL format.
func (u *UDPUpstream) GetDNSStrings() []string {
	url := fmt.Sprintf("udp://%s", u.address)
	if u.Domain != "" {
		url = fmt.Sprintf("%s?domain=%s", url, u.Domain)
	}
	return []string{url}
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
