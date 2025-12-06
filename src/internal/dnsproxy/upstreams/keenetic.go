package upstreams

import (
	"fmt"
	"net"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/core"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

const (
	// DNS protocol defaults
	keeneticDefaultDNSPort = "53"
)

// KeeneticProvider implements UpstreamProvider using Keenetic RCI to get DNS servers.
// It is stateless and fetches DNS servers on each GetUpstreams() call.
// Caching and periodic refresh is handled by MultiUpstream.
// Note: Domain restrictions come from individual DNS servers returned by Keenetic RCI,
// not from the provider itself.
type KeeneticProvider struct {
	keeneticClient core.KeeneticClient
}

// NewKeeneticProvider creates a new Keenetic upstream provider.
func NewKeeneticProvider(keeneticClient core.KeeneticClient, restrictedDomain string) *KeeneticProvider {
	// Note: restrictedDomain parameter is ignored - Keenetic RCI already provides
	// domain restrictions per DNS server
	return &KeeneticProvider{
		keeneticClient: keeneticClient,
	}
}

// GetUpstreams fetches DNS servers from Keenetic and converts them to upstreams.
// This method is stateless and fetches fresh data on each call.
// Each DNS server from Keenetic already has its own domain restriction if applicable.
func (k *KeeneticProvider) GetUpstreams() ([]Upstream, error) {
	// Fetch DNS servers from Keenetic
	dnsServers, err := k.keeneticClient.GetDNSServers()
	if err != nil {
		return nil, fmt.Errorf("failed to fetch Keenetic DNS servers: %w", err)
	}

	// Create upstreams from all servers (each server has its own domain restriction from RCI)
	upstreams := make([]Upstream, 0, len(dnsServers))
	for _, server := range dnsServers {
		upstream := createUpstreamFromDNSServerInfo(server)
		if upstream != nil {
			upstreams = append(upstreams, upstream)
		}
	}

	log.Debugf("Fetched %d DNS servers from Keenetic", len(upstreams))
	return upstreams, nil
}

// String returns a human-readable representation of the provider.
func (k *KeeneticProvider) String() string {
	return "keenetic://"
}

// GetDomain returns empty string as Keenetic provider doesn't have a single domain restriction.
// Domain restrictions are per-server from Keenetic RCI.
func (k *KeeneticProvider) GetDomain() string {
	return ""
}

// Close closes any resources held by the provider.
// Since the provider is stateless, this is a no-op.
func (k *KeeneticProvider) Close() error {
	return nil
}

// GetDNSServers returns the current DNS server info from Keenetic.
// This fetches fresh data on each call.
func (k *KeeneticProvider) GetDNSServers() []keenetic.DNSServerInfo {
	dnsServers, err := k.keeneticClient.GetDNSServers()
	if err != nil {
		log.Warnf("Failed to fetch Keenetic DNS servers: %v", err)
		return nil
	}

	// Return all servers as-is (each already has its domain restriction from RCI)
	return dnsServers
}

// createUpstreamFromDNSServerInfo creates an Upstream from a DNSServerInfo.
// All Keenetic DNS types (Plain, DoT, DoH) connect to local proxy via UDP.
func createUpstreamFromDNSServerInfo(info keenetic.DNSServerInfo) Upstream {
	// Get domain restriction from the server info
	var restrictedDomain string
	if info.Domain != nil {
		restrictedDomain = *info.Domain
	}

	// Validate known DNS server types
	switch info.Type {
	case keenetic.DNSServerTypePlain, keenetic.DNSServerTypePlainIPv6,
		keenetic.DNSServerTypeDoT, keenetic.DNSServerTypeDoH:
		// All types handled below
	default:
		log.Warnf("Unknown Keenetic DNS server type: %s", info.Type)
		return nil
	}

	// Build address with port
	// DoT and DoH connect to local Keenetic proxy that handles TLS/HTTPS
	address := info.Proxy
	if info.Port != "" {
		address = net.JoinHostPort(info.Proxy, info.Port)
	} else if !strings.Contains(address, ":") || info.Type == keenetic.DNSServerTypePlainIPv6 {
		// For IPv4 without port or IPv6, add default port
		address = net.JoinHostPort(address, keeneticDefaultDNSPort)
	}

	upstream, err := NewUDPUpstream(address, restrictedDomain)
	if err != nil {
		log.Warnf("Failed to create upstream for %s: %v", info.Type, err)
		return nil
	}

	return upstream
}
