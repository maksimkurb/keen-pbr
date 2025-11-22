package upstreams

import (
	"context"
	"fmt"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/miekg/dns"
)

// MultiUpstream wraps multiple upstreams and routes queries based on domain.
// It supports both domain-specific upstreams and fallback upstreams (no domain restriction).
type MultiUpstream struct {
	upstreams []Upstream
}

// NewMultiUpstream creates a new multi-upstream.
func NewMultiUpstream(upstreams []Upstream) *MultiUpstream {
	return &MultiUpstream{upstreams: upstreams}
}

// Query routes the query to the appropriate upstream based on domain matching.
// It first tries domain-specific upstreams, then falls back to general upstreams.
func (m *MultiUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	if len(m.upstreams) == 0 {
		return nil, fmt.Errorf("no upstreams configured")
	}

	// Get the query domain
	var queryDomain string
	if len(req.Question) > 0 {
		queryDomain = req.Question[0].Name
	}

	// First, try domain-specific upstreams
	var lastErr error
	for _, upstream := range m.upstreams {
		// Skip upstreams that don't match the domain
		if upstream.GetDomain() != "" && !upstream.MatchesDomain(queryDomain) {
			continue
		}

		// Prefer domain-specific upstreams
		if upstream.GetDomain() != "" {
			resp, err := upstream.Query(ctx, req)
			if err != nil {
				lastErr = err
				log.Debugf("Domain-specific upstream %s failed: %v", upstream, err)
				continue
			}
			return resp, nil
		}
	}

	// Then, try general upstreams (no domain restriction)
	for _, upstream := range m.upstreams {
		if upstream.GetDomain() != "" {
			continue // Skip domain-specific upstreams
		}

		resp, err := upstream.Query(ctx, req)
		if err != nil {
			lastErr = err
			log.Debugf("Upstream %s failed: %v", upstream, err)
			continue
		}
		return resp, nil
	}

	return nil, fmt.Errorf("all upstreams failed, last error: %w", lastErr)
}

// String returns a human-readable representation of all upstreams.
func (m *MultiUpstream) String() string {
	var parts []string
	for _, upstream := range m.upstreams {
		parts = append(parts, upstream.String())
	}
	return strings.Join(parts, ", ")
}

// Close closes all upstreams.
func (m *MultiUpstream) Close() error {
	for _, upstream := range m.upstreams {
		upstream.Close()
	}
	return nil
}

// GetDNSServers returns all DNS servers from all upstreams.
func (m *MultiUpstream) GetDNSServers() []keenetic.DNSServerInfo {
	var servers []keenetic.DNSServerInfo
	for _, upstream := range m.upstreams {
		servers = append(servers, upstream.GetDNSServers()...)
	}
	return servers
}

// GetDomain returns empty string as MultiUpstream doesn't have a single domain.
func (m *MultiUpstream) GetDomain() string {
	return ""
}

// MatchesDomain always returns true as MultiUpstream handles routing internally.
func (m *MultiUpstream) MatchesDomain(domain string) bool {
	return true
}
