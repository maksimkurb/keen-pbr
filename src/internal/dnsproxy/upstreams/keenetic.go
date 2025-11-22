package upstreams

import (
	"context"
	"fmt"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/miekg/dns"
)

const (
	// Cache configuration
	keeneticCacheTTL = 5 * time.Minute // How long to cache DNS server list from Keenetic

	// DNS protocol defaults
	keeneticDefaultDNSPort = "53"
)

// KeeneticUpstream implements Upstream using Keenetic RCI to get DNS servers.
type KeeneticUpstream struct {
	BaseUpstream
	keeneticClient domain.KeeneticClient

	mu            sync.RWMutex
	upstreams     []Upstream
	cachedServers []keenetic.DNSServerInfo // Raw server info for API display
	lastFetch     time.Time
	cacheTTL      time.Duration
}

// NewKeeneticUpstream creates a new Keenetic upstream.
// The restrictedDomain parameter restricts the upstream to a specific domain (empty = all domains).
func NewKeeneticUpstream(keeneticClient domain.KeeneticClient, restrictedDomain string) *KeeneticUpstream {
	return &KeeneticUpstream{
		BaseUpstream:   BaseUpstream{Domain: restrictedDomain},
		keeneticClient: keeneticClient,
		cacheTTL:       keeneticCacheTTL,
	}
}

// Query sends a DNS query to one of the Keenetic DNS servers.
func (k *KeeneticUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	upstreams, err := k.getUpstreams()
	if err != nil {
		return nil, fmt.Errorf("failed to get Keenetic upstreams: %w", err)
	}

	if len(upstreams) == 0 {
		return nil, fmt.Errorf("no Keenetic DNS servers available")
	}

	// Try each upstream in order until one succeeds
	var lastErr error
	for _, upstream := range upstreams {
		resp, err := upstream.Query(ctx, req)
		if err != nil {
			lastErr = err
			log.Debugf("Keenetic upstream %s failed: %v", upstream, err)
			continue
		}
		return resp, nil
	}

	return nil, fmt.Errorf("all Keenetic upstreams failed, last error: %w", lastErr)
}

// String returns a human-readable representation of the upstream.
func (k *KeeneticUpstream) String() string {
	if k.Domain != "" {
		return fmt.Sprintf("keenetic:// (domain: %s)", k.Domain)
	}
	return "keenetic://"
}

// Close closes any resources held by the upstream.
func (k *KeeneticUpstream) Close() error {
	k.mu.Lock()
	defer k.mu.Unlock()

	for _, upstream := range k.upstreams {
		upstream.Close()
	}
	k.upstreams = nil
	k.cachedServers = nil
	return nil
}

// GetDNSServers returns the cached DNS server info from Keenetic.
func (k *KeeneticUpstream) GetDNSServers() []keenetic.DNSServerInfo {
	// Trigger a fetch if needed (this updates cachedServers)
	_, _ = k.getUpstreams()

	k.mu.RLock()
	defer k.mu.RUnlock()
	return k.cachedServers
}

// getUpstreams returns the cached upstreams or fetches them from Keenetic.
func (k *KeeneticUpstream) getUpstreams() ([]Upstream, error) {
	k.mu.RLock()
	if time.Since(k.lastFetch) < k.cacheTTL && len(k.upstreams) > 0 {
		upstreams := k.upstreams
		k.mu.RUnlock()
		return upstreams, nil
	}
	k.mu.RUnlock()

	k.mu.Lock()
	defer k.mu.Unlock()

	// Double-check after acquiring write lock
	if time.Since(k.lastFetch) < k.cacheTTL && len(k.upstreams) > 0 {
		return k.upstreams, nil
	}

	// Fetch DNS servers from Keenetic
	dnsServers, err := k.keeneticClient.GetDNSServers()
	if err != nil {
		// If we have cached upstreams, return them even if stale
		if len(k.upstreams) > 0 {
			log.Warnf("Failed to fetch Keenetic DNS servers, using cached: %v", err)
			return k.upstreams, nil
		}
		return nil, err
	}

	// Close old upstreams
	for _, upstream := range k.upstreams {
		upstream.Close()
	}

	// Create new upstreams from DnsServerInfo
	// Filter servers based on this upstream's domain restriction:
	// - If this upstream has no domain restriction, only use general servers (no domain)
	// - If this upstream has a domain restriction, only use servers matching that domain
	k.upstreams = make([]Upstream, 0, len(dnsServers))
	k.cachedServers = make([]keenetic.DNSServerInfo, 0, len(dnsServers))
	for _, server := range dnsServers {
		// Check if server matches this upstream's domain restriction
		if k.Domain == "" {
			// General upstream: skip domain-specific servers
			if server.Domain != nil {
				continue
			}
		} else {
			// Domain-specific upstream: only use servers matching this domain
			if server.Domain == nil || *server.Domain != k.Domain {
				continue
			}
		}

		upstream := createUpstreamFromDNSServerInfo(server)
		if upstream != nil {
			k.upstreams = append(k.upstreams, upstream)
			k.cachedServers = append(k.cachedServers, server)
		}
	}

	k.lastFetch = time.Now()
	log.Debugf("Fetched %d DNS servers from Keenetic", len(k.upstreams))

	return k.upstreams, nil
}

// createUpstreamFromDNSServerInfo creates an Upstream from a DnsServerInfo.
func createUpstreamFromDNSServerInfo(info keenetic.DNSServerInfo) Upstream {
	// Get domain restriction from the server info
	var restrictedDomain string
	if info.Domain != nil {
		restrictedDomain = *info.Domain
	}

	switch info.Type {
	case keenetic.DNSServerTypePlain, keenetic.DNSServerTypePlainIPv6:
		address := info.Proxy
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else if !strings.Contains(address, ":") || info.Type == keenetic.DNSServerTypePlainIPv6 {
			// For IPv4 without port or IPv6, add default port
			address = net.JoinHostPort(address, keeneticDefaultDNSPort)
		}
		upstream, err := NewUDPUpstream(address, restrictedDomain)
		if err != nil {
			log.Warnf("Failed to create UDP upstream: %v", err)
			return nil
		}
		return upstream

	case keenetic.DNSServerTypeDoT:
		// DoT: connect to local proxy that handles TLS
		var address string
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else {
			address = net.JoinHostPort(info.Proxy, keeneticDefaultDNSPort)
		}
		upstream, err := NewUDPUpstream(address, restrictedDomain)
		if err != nil {
			log.Warnf("Failed to create DoT upstream: %v", err)
			return nil
		}
		return upstream

	case keenetic.DNSServerTypeDoH:
		// DoH: connect to local proxy that handles HTTPS
		var address string
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else {
			address = net.JoinHostPort(info.Proxy, keeneticDefaultDNSPort)
		}
		upstream, err := NewUDPUpstream(address, restrictedDomain)
		if err != nil {
			log.Warnf("Failed to create DoH upstream: %v", err)
			return nil
		}
		return upstream

	default:
		log.Warnf("Unknown DNS server type: %s", info.Type)
		return nil
	}
}
