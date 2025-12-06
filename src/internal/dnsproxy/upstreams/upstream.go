// Package upstreams provides DNS upstream resolver implementations.
package upstreams

import (
	"context"
	"fmt"
	"net/url"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/core"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/miekg/dns"
)

// UpstreamProvider is a container that provides a list of upstreams.
// This is used for dynamic upstream sources like Keenetic that periodically
// fetch DNS server lists and convert them to plain upstreams.
type UpstreamProvider interface {
	// GetUpstreams returns the current list of upstreams.
	// The list may change over time as the provider updates its configuration.
	GetUpstreams() ([]Upstream, error)
	// GetDomain returns the domain this provider is restricted to (empty = all domains).
	GetDomain() string
	// String returns a human-readable representation of the provider.
	String() string
	// Close closes any resources held by the provider.
	Close() error
	// GetDNSServers returns the list of DNS servers this provider uses (for API display).
	GetDNSServers() []keenetic.DNSServerInfo
}

// Upstream represents a DNS upstream resolver.
type Upstream interface {
	// Query sends a DNS query to the upstream and returns the response.
	Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error)
	// Close closes any resources held by the upstream.
	Close() error
	// GetDomain returns the domain this upstream is restricted to (empty = all domains).
	GetDomain() string
	// MatchesDomain returns true if this upstream should handle the given domain.
	MatchesDomain(domain string) bool
	// GetDNSStrings returns an array of DNS server strings in URL format.
	// Format: "protocol://address?domain=example.com" (domain param is optional)
	GetDNSStrings() []string
}

// BaseUpstream provides common functionality for all upstreams.
type BaseUpstream struct {
	// Domain restricts this upstream to a specific domain and its subdomains.
	// Empty string means this upstream can be used for any domain.
	Domain string
	// normalizedDomain is the pre-computed normalized version of Domain
	// (lowercase, no trailing dot). Empty if Domain is empty.
	normalizedDomain string
}

// NewBaseUpstream creates a BaseUpstream with pre-normalized domain.
// This reduces allocations in the hot path (MatchesDomain).
func NewBaseUpstream(domain string) BaseUpstream {
	normalized := ""
	if domain != "" {
		normalized = strings.ToLower(strings.TrimSuffix(domain, "."))
	}
	return BaseUpstream{
		Domain:           domain,
		normalizedDomain: normalized,
	}
}

// GetDomain returns the domain this upstream is restricted to.
func (b *BaseUpstream) GetDomain() string {
	return b.Domain
}

// MatchesDomain returns true if this upstream should handle the given domain.
func (b *BaseUpstream) MatchesDomain(queryDomain string) bool {
	if b.normalizedDomain == "" {
		return true // No restriction, matches all domains
	}

	// Normalize query domain once (can't avoid since it's input)
	normalizedQuery := strings.ToLower(strings.TrimSuffix(queryDomain, "."))

	// Exact match
	if normalizedQuery == b.normalizedDomain {
		return true
	}

	// Subdomain match (query is a subdomain of restricted domain)
	if strings.HasSuffix(normalizedQuery, "."+b.normalizedDomain) {
		return true
	}

	return false
}

// ParseUpstream parses an upstream URL and returns either an Upstream or UpstreamProvider.
// Supported formats:
//   - keenetic:// - use Keenetic RCI to get DNS servers (returns UpstreamProvider)
//   - udp://ip:port - plain UDP DNS (port defaults to 53)
//   - doh://host/path - DNS-over-HTTPS
//
// The domain parameter restricts the upstream to a specific domain (empty = all domains).
// Returns (upstream, provider, error) - exactly one of upstream or provider will be non-nil.
func ParseUpstream(upstreamURL string, keeneticClient core.KeeneticClient, restrictedDomain string) (Upstream, UpstreamProvider, error) {
	if strings.HasPrefix(upstreamURL, "keenetic://") {
		return parseKeeneticProvider(keeneticClient, restrictedDomain)
	}

	u, err := url.Parse(upstreamURL)
	// If url.Parse fails (e.g. "8.8.8.8:53"), or scheme is empty, try as UDP upstream
	if err != nil || u.Scheme == "" {
		upstream, err := parseUDPUpstream(upstreamURL, restrictedDomain)
		return upstream, nil, err
	}

	switch u.Scheme {
	case "udp":
		upstream, err := parseUDPUpstream(u.Host, restrictedDomain)
		return upstream, nil, err
	case "doh", "https":
		return NewDoHUpstream(upstreamURL, restrictedDomain), nil, nil
	default:
		return nil, nil, fmt.Errorf("unsupported upstream scheme: %s", u.Scheme)
	}
}

func parseKeeneticProvider(keeneticClient core.KeeneticClient, restrictedDomain string) (Upstream, UpstreamProvider, error) {
	if keeneticClient == nil {
		return nil, nil, fmt.Errorf("keenetic:// upstream requires KeeneticClient")
	}
	return nil, NewKeeneticProvider(keeneticClient, restrictedDomain), nil
}

func parseUDPUpstream(address string, restrictedDomain string) (Upstream, error) {
	return NewUDPUpstream(address, restrictedDomain)
}
