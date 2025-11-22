// Package upstreams provides DNS upstream resolver implementations.
package upstreams

import (
	"context"
	"fmt"
	"net/url"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/miekg/dns"
)

// Upstream represents a DNS upstream resolver.
type Upstream interface {
	// Query sends a DNS query to the upstream and returns the response.
	Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error)
	// String returns a human-readable representation of the upstream.
	String() string
	// Close closes any resources held by the upstream.
	Close() error
	// GetDNSServers returns the list of DNS servers this upstream uses.
	GetDNSServers() []keenetic.DNSServerInfo
	// GetDomain returns the domain this upstream is restricted to (empty = all domains).
	GetDomain() string
	// MatchesDomain returns true if this upstream should handle the given domain.
	MatchesDomain(domain string) bool
}

// BaseUpstream provides common functionality for all upstreams.
type BaseUpstream struct {
	// Domain restricts this upstream to a specific domain and its subdomains.
	// Empty string means this upstream can be used for any domain.
	Domain string
}

// GetDomain returns the domain this upstream is restricted to.
func (b *BaseUpstream) GetDomain() string {
	return b.Domain
}

// MatchesDomain returns true if this upstream should handle the given domain.
func (b *BaseUpstream) MatchesDomain(queryDomain string) bool {
	if b.Domain == "" {
		return true // No restriction, matches all domains
	}

	// Normalize domains (remove trailing dots, lowercase)
	queryDomain = strings.ToLower(strings.TrimSuffix(queryDomain, "."))
	restrictedDomain := strings.ToLower(strings.TrimSuffix(b.Domain, "."))

	// Exact match
	if queryDomain == restrictedDomain {
		return true
	}

	// Subdomain match (query is a subdomain of restricted domain)
	if strings.HasSuffix(queryDomain, "."+restrictedDomain) {
		return true
	}

	return false
}

// ParseUpstream parses an upstream URL and returns the appropriate Upstream implementation.
// Supported formats:
//   - keenetic:// - use Keenetic RCI to get DNS servers
//   - udp://ip:port - plain UDP DNS (port defaults to 53)
//   - doh://host/path - DNS-over-HTTPS
//
// The domain parameter restricts the upstream to a specific domain (empty = all domains).
func ParseUpstream(upstreamURL string, keeneticClient domain.KeeneticClient, restrictedDomain string) (Upstream, error) {
	if strings.HasPrefix(upstreamURL, "keenetic://") {
		return parseKeeneticUpstream(keeneticClient, restrictedDomain)
	}

	u, err := url.Parse(upstreamURL)
	// If url.Parse fails (e.g. "8.8.8.8:53"), or scheme is empty, try as UDP upstream
	if err != nil || u.Scheme == "" {
		return parseUDPUpstream(upstreamURL, restrictedDomain)
	}

	switch u.Scheme {
	case "udp":
		return parseUDPUpstream(u.Host, restrictedDomain)
	case "doh", "https":
		return NewDoHUpstream(upstreamURL, restrictedDomain), nil
	default:
		return nil, fmt.Errorf("unsupported upstream scheme: %s", u.Scheme)
	}
}

func parseKeeneticUpstream(keeneticClient domain.KeeneticClient, restrictedDomain string) (Upstream, error) {
	if keeneticClient == nil {
		return nil, fmt.Errorf("keenetic:// upstream requires KeeneticClient")
	}
	return NewKeeneticUpstream(keeneticClient, restrictedDomain), nil
}

func parseUDPUpstream(address string, restrictedDomain string) (Upstream, error) {
	return NewUDPUpstream(address, restrictedDomain)
}
