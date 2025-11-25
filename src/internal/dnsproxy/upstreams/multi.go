package upstreams

import (
	"context"
	"fmt"
	"math/rand"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/miekg/dns"
)

const (
	// How often to refresh provider upstreams
	providerRefreshInterval = 1 * time.Minute
)

// MultiUpstream wraps multiple upstreams and upstream providers, routing queries based on domain.
// It expands providers into plain upstreams and selects randomly from available upstreams.
// It caches the expanded upstreams and refreshes them periodically.
type MultiUpstream struct {
	staticUpstreams []Upstream
	providers       []UpstreamProvider

	mu                sync.RWMutex
	expandedUpstreams []Upstream // Cached list of all upstreams (static + expanded from providers)

	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup
}

// NewMultiUpstream creates a new multi-upstream from a mix of upstreams and providers.
// It immediately expands providers and starts a background goroutine to refresh them periodically.
func NewMultiUpstream(upstreams []Upstream, providers []UpstreamProvider) *MultiUpstream {
	ctx, cancel := context.WithCancel(context.Background())
	m := &MultiUpstream{
		staticUpstreams: upstreams,
		providers:       providers,
		ctx:             ctx,
		cancel:          cancel,
	}

	// Initial expansion of providers
	m.refreshUpstreams()

	// Start periodic refresh if we have providers
	if len(providers) > 0 {
		m.wg.Add(1)
		go m.refreshLoop()
	}

	return m
}

// refreshLoop periodically refreshes provider upstreams.
func (m *MultiUpstream) refreshLoop() {
	defer m.wg.Done()

	ticker := time.NewTicker(providerRefreshInterval)
	defer ticker.Stop()

	for {
		select {
		case <-m.ctx.Done():
			return
		case <-ticker.C:
			m.refreshUpstreams()
		}
	}
}

// refreshUpstreams expands all providers and updates the cached upstream list.
func (m *MultiUpstream) refreshUpstreams() {
	var allUpstreams []Upstream

	// Add static upstreams
	allUpstreams = append(allUpstreams, m.staticUpstreams...)

	// Expand providers
	for _, provider := range m.providers {
		upstreams, err := provider.GetUpstreams()
		if err != nil {
			log.Warnf("Failed to get upstreams from provider %s: %v", provider.String(), err)
			continue
		}
		allUpstreams = append(allUpstreams, upstreams...)
	}

	// Update cached list
	m.mu.Lock()
	// Close old provider-generated upstreams (but not static ones)
	if m.expandedUpstreams != nil {
		for i := len(m.staticUpstreams); i < len(m.expandedUpstreams); i++ {
			if err := m.expandedUpstreams[i].Close(); err != nil {
				log.Warnf("Failed to close upstream: %v", err)
			}
		}
	}
	m.expandedUpstreams = allUpstreams
	m.mu.Unlock()

	log.Debugf("Refreshed upstreams: %d total (%d static, %d from providers)",
		len(allUpstreams), len(m.staticUpstreams), len(allUpstreams)-len(m.staticUpstreams))
}

// Query routes the query to the appropriate upstream based on domain matching.
// It first tries domain-specific upstreams, then falls back to general upstreams.
// For each category, it selects a random upstream if multiple are available.
func (m *MultiUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	// Get the query domain
	var queryDomain string
	if len(req.Question) > 0 {
		queryDomain = req.Question[0].Name
	}

	// Get cached upstreams
	m.mu.RLock()
	allUpstreams := m.expandedUpstreams
	m.mu.RUnlock()

	if len(allUpstreams) == 0 {
		return nil, fmt.Errorf("no upstreams configured")
	}

	// Separate domain-specific and general upstreams that match the query
	var domainSpecific []Upstream
	var general []Upstream

	for _, upstream := range allUpstreams {
		if upstream.GetDomain() != "" && upstream.MatchesDomain(queryDomain) {
			domainSpecific = append(domainSpecific, upstream)
		} else if upstream.GetDomain() == "" {
			general = append(general, upstream)
		}
	}

	// Try domain-specific upstreams first (random selection)
	if len(domainSpecific) > 0 {
		if resp, err := m.tryUpstreams(ctx, req, domainSpecific); err == nil {
			return resp, nil
		}
	}

	// Fall back to general upstreams (random selection)
	if len(general) > 0 {
		if resp, err := m.tryUpstreams(ctx, req, general); err == nil {
			return resp, nil
		}
	}

	return nil, fmt.Errorf("all upstreams failed for query %s", queryDomain)
}

// tryUpstreams tries to query the given upstreams by picking one randomly.
func (m *MultiUpstream) tryUpstreams(ctx context.Context, req *dns.Msg, upstreams []Upstream) (*dns.Msg, error) {
	if len(upstreams) == 0 {
		return nil, fmt.Errorf("no upstreams available")
	}

	// Pick a random upstream
	idx := rand.Intn(len(upstreams))
	upstream := upstreams[idx]

	resp, err := upstream.Query(ctx, req)
	if err != nil {
		log.Debugf("Upstream %s failed: %v", upstream, err)
		return nil, fmt.Errorf("upstream failed: %w", err)
	}

	return resp, nil
}

// Close closes all upstreams and providers and stops the refresh goroutine.
func (m *MultiUpstream) Close() error {
	// Stop refresh goroutine
	m.cancel()
	m.wg.Wait()

	// Close all upstreams
	m.mu.Lock()
	for _, upstream := range m.expandedUpstreams {
		if err := upstream.Close(); err != nil {
			log.Warnf("Failed to close upstream: %v", err)
		}
	}
	m.expandedUpstreams = nil
	m.mu.Unlock()

	// Close providers
	for _, provider := range m.providers {
		if err := provider.Close(); err != nil {
			log.Warnf("Failed to close provider: %v", err)
		}
	}
	return nil
}

// GetDNSStrings returns all DNS server strings from cached upstreams.
// This includes both static upstreams and provider-generated upstreams from cache.
// It does not re-query providers.
func (m *MultiUpstream) GetDNSStrings() []string {
	m.mu.RLock()
	allUpstreams := m.expandedUpstreams
	m.mu.RUnlock()

	var dnsStrings []string
	for _, upstream := range allUpstreams {
		dnsStrings = append(dnsStrings, upstream.GetDNSStrings()...)
	}
	return dnsStrings
}

// GetDomain returns empty string as MultiUpstream doesn't have a single domain.
func (m *MultiUpstream) GetDomain() string {
	return ""
}

// MatchesDomain always returns true as MultiUpstream handles routing internally.
func (m *MultiUpstream) MatchesDomain(domain string) bool {
	return true
}
