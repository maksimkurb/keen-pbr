package dnsproxy

import (
	"context"
	"fmt"
	"net"
	"net/netip"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/core"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/caching"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/matcher"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/upstreams"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/miekg/dns"
)

// ProxyHandler implements the Handler interface with the application's specific logic:
// caching, upstream forwarding, domain matching, and ipset integration.
// This component has NO knowledge of network I/O (sockets, workers, etc.).
type ProxyHandler struct {
	config ProxyConfig

	// Dependencies
	keeneticClient core.KeeneticClient
	ipsetManager   core.IPSetManager
	appConfig      *config.Config

	// Upstream resolvers
	upstream upstreams.Upstream

	// Upstream providers (for API display of DNS servers)
	providers []upstreams.UpstreamProvider

	// Per-ipset DNS overrides
	ipsetUpstreams map[string]upstreams.Upstream

	// IPSet name to config mapping for O(1) lookups
	ipsetsByName map[string]*config.IPSetConfig

	// Domain matcher for routing decisions
	matcher *matcher.Matcher

	// Records cache for CNAME tracking
	recordsCache *caching.RecordsCache

	// SSE broadcasting for DNS check
	dnscheckSubscribersMu sync.RWMutex
	dnscheckSubscribers   map[chan string]struct{}

	// Lifecycle (for cleanup loop)
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup

	// Buffer pools for zero-allocation processing
	ipsetEntrySlicePool sync.Pool // []networking.IPSetEntry slices
	cachedAddrSlicePool sync.Pool // []caching.CachedAddress slices
}

// NewProxyHandler creates a new handler with all the business logic components.
func NewProxyHandler(
	cfg ProxyConfig,
	keeneticClient core.KeeneticClient,
	ipsetManager core.IPSetManager,
	appConfig *config.Config,
) (*ProxyHandler, error) {
	ctx, cancel := context.WithCancel(context.Background())

	maxCacheDomains := cfg.MaxCacheDomains
	if maxCacheDomains <= 0 {
		maxCacheDomains = 1000 // default - reduced for memory efficiency on embedded devices
	}

	// Build ipsetsByName map for O(1) lookups
	ipsetsByName := make(map[string]*config.IPSetConfig, len(appConfig.IPSets))
	for _, ipset := range appConfig.IPSets {
		ipsetsByName[ipset.IPSetName] = ipset
	}

	handler := &ProxyHandler{
		config:              cfg,
		keeneticClient:      keeneticClient,
		ipsetManager:        ipsetManager,
		appConfig:           appConfig,
		recordsCache:        caching.NewRecordsCache(maxCacheDomains),
		ipsetUpstreams:      make(map[string]upstreams.Upstream),
		ipsetsByName:        ipsetsByName,
		dnscheckSubscribers: make(map[chan string]struct{}),
		ctx:                 ctx,
		cancel:              cancel,
	}

	// Parse default upstream(s) and providers
	var upstreamList []upstreams.Upstream
	var providerList []upstreams.UpstreamProvider
	for _, upstreamURL := range cfg.Upstreams {
		upstream, provider, err := upstreams.ParseUpstream(upstreamURL, keeneticClient, "")
		if err != nil {
			cancel()
			return nil, fmt.Errorf("failed to parse upstream %q: %w", upstreamURL, err)
		}
		if upstream != nil {
			upstreamList = append(upstreamList, upstream)
		}
		if provider != nil {
			providerList = append(providerList, provider)
		}
	}

	// Create the main upstream (single, multi, or error)
	totalCount := len(upstreamList) + len(providerList)
	if totalCount == 0 {
		cancel()
		return nil, fmt.Errorf("no upstreams configured")
	} else if totalCount == 1 && len(upstreamList) == 1 {
		// Single static upstream
		handler.upstream = upstreamList[0]
	} else {
		// Multiple upstreams/providers or single provider
		handler.upstream = upstreams.NewMultiUpstream(upstreamList, providerList)
	}

	// Store providers for API access
	handler.providers = providerList

	// Parse per-ipset DNS overrides (optional per-ipset DNS routing)
	for _, ipset := range appConfig.IPSets {
		if ipset.Routing != nil && ipset.Routing.DNS != nil && len(ipset.Routing.DNS.Upstreams) > 0 {
			// Parse all upstreams for this ipset
			var ipsetUpstreamList []upstreams.Upstream
			var ipsetProviders []upstreams.UpstreamProvider

			for _, upstreamURL := range ipset.Routing.DNS.Upstreams {
				upstream, provider, err := upstreams.ParseUpstream(upstreamURL, keeneticClient, "")
				if err != nil {
					log.Warnf("Failed to parse ipset upstream %s: %v", upstreamURL, err)
					continue
				}
				if upstream != nil {
					ipsetUpstreamList = append(ipsetUpstreamList, upstream)
				}
				if provider != nil {
					ipsetProviders = append(ipsetProviders, provider)
				}
			}

			// Wrap in MultiUpstream if multiple upstreams/providers
			var ipsetUpstream upstreams.Upstream
			if len(ipsetUpstreamList) == 1 && len(ipsetProviders) == 0 {
				ipsetUpstream = ipsetUpstreamList[0]
			} else if len(ipsetUpstreamList) > 0 || len(ipsetProviders) > 0 {
				ipsetUpstream = upstreams.NewMultiUpstream(ipsetUpstreamList, ipsetProviders)
			}

			if ipsetUpstream != nil {
				handler.ipsetUpstreams[ipset.IPSetName] = ipsetUpstream
				log.Infof("Configured DNS override for ipset %s: %s", ipset.IPSetName, strings.Join(ipsetUpstream.GetDNSStrings(), ", "))
			}
		}
	}

	// Create domain matcher
	handler.matcher = matcher.NewMatcher(appConfig)

	// Initialize buffer pools for zero-allocation processing
	handler.ipsetEntrySlicePool = sync.Pool{
		New: func() interface{} {
			slice := make([]networking.IPSetEntry, 0, 8)
			return &slice
		},
	}
	handler.cachedAddrSlicePool = sync.Pool{
		New: func() interface{} {
			// Pre-allocate with a capacity of 8, a reasonable number for A/AAAA records
			slice := make([]caching.CachedAddress, 0, 8)
			return &slice
		},
	}

	// Start cleanup goroutine
	handler.wg.Add(1)
	go handler.cleanupLoop()

	return handler, nil
}

// HandleRequest is the implementation of the Handler interface.
// It processes a raw DNS query and returns a raw DNS response.
func (h *ProxyHandler) HandleRequest(clientAddr net.Addr, reqBytes []byte, network string) ([]byte, error) {
	// Parse request
	var reqMsg dns.Msg
	if err := reqMsg.Unpack(reqBytes); err != nil {
		return nil, fmt.Errorf("failed to parse request: %w", err)
	}

	// Log request
	if log.IsVerbose() && len(reqMsg.Question) > 0 {
		q := reqMsg.Question[0]
		log.Debugf("[%04x] DNS query: %s %s from %s via %s",
			reqMsg.Id, q.Name, dns.TypeToString[q.Qtype], clientAddr, network)
	}

	// Check if this is a DNS check domain - intercept and respond immediately
	if respBytes, err := h.processDNSCheckRequest(&reqMsg); respBytes != nil || err != nil {
		return respBytes, err
	}

	// Select upstream: check ipset-specific overrides first, then use default
	selectedUpstream := h.selectUpstream(&reqMsg)

	// Try to get response from cache first
	respMsg := h.getCachedResponse(&reqMsg)
	if respMsg != nil {
		// Cache hit - log and return cached response
		if log.IsVerbose() && len(reqMsg.Question) > 0 {
			q := reqMsg.Question[0]
			log.Debugf("[%04x] Response served from cache for %s %s",
				reqMsg.Id, q.Name, dns.TypeToString[q.Qtype])
		}
	} else {
		// Cache miss - forward to selected upstream
		ctx, cancel := context.WithTimeout(h.ctx, upstreamQueryTimeout)
		defer cancel()

		var err error
		respMsg, err = selectedUpstream.Query(ctx, &reqMsg)
		if err != nil {
			return nil, err
		}

		// Process response (filter AAAA, match domains, add to ipsets)
		h.processResponse(&reqMsg, respMsg)
	}

	// Pack response
	respBytes, err := respMsg.Pack()
	if err != nil {
		return nil, fmt.Errorf("failed to pack response: %w", err)
	}

	return respBytes, nil
}

// selectUpstream selects the appropriate upstream for a DNS query.
// Checks ipset-specific overrides first, then uses default.
func (h *ProxyHandler) selectUpstream(reqMsg *dns.Msg) upstreams.Upstream {
	selectedUpstream := h.upstream // Default upstream

	// Check if any ipset has DNS override matching this query domain
	if len(reqMsg.Question) > 0 {
		queryDomain := reqMsg.Question[0].Name

		for ipsetName, ipsetUpstream := range h.ipsetUpstreams {
			if ipsetUpstream.MatchesDomain(queryDomain) {
				if log.IsVerbose() {
					log.Debugf("[%04x] Using ipset-specific DNS for %s (ipset: %s)", reqMsg.Id, queryDomain, ipsetName)
				}
				selectedUpstream = ipsetUpstream
				break
			}
		}
	}

	return selectedUpstream
}

// getCachedResponse attempts to build a DNS response from cached data.
// Returns nil if cache miss or data expired.
func (h *ProxyHandler) getCachedResponse(reqMsg *dns.Msg) *dns.Msg {
	if len(reqMsg.Question) == 0 {
		return nil
	}

	q := reqMsg.Question[0]
	if q.Qtype != dns.TypeA && q.Qtype != dns.TypeAAAA {
		return nil // Only cache A/AAAA queries
	}

	domain := normalizeDomain(q.Name)

	// Get CNAME chain (if any)
	chain := h.recordsCache.GetTargetChain(domain)
	if len(chain) == 0 {
		return nil // No data in cache
	}

	// Get a pooled slice for the results
	validAddrsPtr := h.cachedAddrSlicePool.Get().(*[]caching.CachedAddress)
	validAddrs := (*validAddrsPtr)[:0] // Reset slice length to 0
	defer func() {
		// Clear slice to prevent memory leaks and return to pool
		for i := range validAddrs {
			validAddrs[i] = caching.CachedAddress{} // Zero out the structs
		}
		*validAddrsPtr = validAddrs
		h.cachedAddrSlicePool.Put(validAddrsPtr)
	}()

	// Get addresses for final target in chain, filtering by qtype directly
	finalTarget := chain[len(chain)-1]
	validAddrs = h.recordsCache.GetFilteredAddresses(finalTarget, q.Qtype, validAddrs)

	if len(validAddrs) == 0 {
		return nil // No matching addresses for this type
	}

	// Build response
	respMsg := new(dns.Msg)
	respMsg.SetReply(reqMsg)
	respMsg.Authoritative = false
	respMsg.RecursionAvailable = true

	now := time.Now().Unix()

	// Add CNAME records to Answer section (if chain > 1)
	if len(chain) > 1 {
		// Use the TTL from the first valid address for all CNAMEs in the chain.
		// This is a reasonable approximation.
		remaining := validAddrs[0].Deadline.Unix() - now
		ttl := uint32(0)
		if remaining > 0 {
			ttl = uint32(remaining)
		}

		for i := 0; i < len(chain)-1; i++ {
			cname := &dns.CNAME{
				Hdr: dns.RR_Header{
					Name:   dns.Fqdn(chain[i]),
					Rrtype: dns.TypeCNAME,
					Class:  dns.ClassINET,
					Ttl:    ttl,
				},
				Target: dns.Fqdn(chain[i+1]),
			}
			respMsg.Answer = append(respMsg.Answer, cname)
		}
	}

	// Add A/AAAA records to Answer section
	for _, addr := range validAddrs {
		remaining := addr.Deadline.Unix() - now
		if remaining <= 0 {
			continue // Should be rare due to cache logic, but good for safety
		}
		ttl := uint32(remaining)

		if q.Qtype == dns.TypeA {
			a := &dns.A{
				Hdr: dns.RR_Header{
					Name:   dns.Fqdn(finalTarget),
					Rrtype: dns.TypeA,
					Class:  dns.ClassINET,
					Ttl:    ttl,
				},
				A: addr.Address,
			}
			respMsg.Answer = append(respMsg.Answer, a)
		} else { // TypeAAAA
			aaaa := &dns.AAAA{
				Hdr: dns.RR_Header{
					Name:   dns.Fqdn(finalTarget),
					Rrtype: dns.TypeAAAA,
					Class:  dns.ClassINET,
					Ttl:    ttl,
				},
				AAAA: addr.Address,
			}
			respMsg.Answer = append(respMsg.Answer, aaaa)
		}
	}

	return respMsg
}

// processResponse processes a DNS response, filtering AAAA records if configured
// and adding resolved IPs to ipsets.
func (h *ProxyHandler) processResponse(reqMsg, respMsg *dns.Msg) {
	// 1. Filter AAAA if configured (existing logic - in-place)
	if h.config.DropAAAA {
		n := 0
		for _, rr := range respMsg.Answer {
			if _, ok := rr.(*dns.AAAA); !ok {
				respMsg.Answer[n] = rr
				n++
			}
		}
		respMsg.Answer = respMsg.Answer[:n]
	}

	// 2. Early exit if not successful
	if respMsg.Rcode != dns.RcodeSuccess {
		return
	}

	// 3. Stream processing: iterate through answer records once
	// Track domain matches to avoid duplicate matcher lookups
	domainMatches := make(map[string][]string, len(respMsg.Answer))

	// Lazy allocation: only get pooled slice if we find a matched domain
	var entries []networking.IPSetEntry
	var entriesPtr *[]networking.IPSetEntry

	for _, rr := range respMsg.Answer {
		if rr == nil {
			continue
		}

		switch v := rr.(type) {
		case *dns.A:
			domain := normalizeDomain(v.Hdr.Name)
			ttl := v.Hdr.Ttl

			if log.IsVerbose() {
				log.Debugf("[%04x] A record: %s -> %s (TTL: %d)", reqMsg.Id, domain, v.A, ttl)
			}

			// Lazy allocate entries slice on first match
			if entries == nil {
				entriesPtr = h.ipsetEntrySlicePool.Get().(*[]networking.IPSetEntry)
				entries = (*entriesPtr)[:0]
			}

			entries = h.processAddressRecord(reqMsg.Id, domain, v.A, ttl, domainMatches, entries)

		case *dns.AAAA:
			domain := normalizeDomain(v.Hdr.Name)
			ttl := v.Hdr.Ttl

			if log.IsVerbose() {
				log.Debugf("[%04x] AAAA record: %s -> %s (TTL: %d)", reqMsg.Id, domain, v.AAAA, ttl)
			}

			// Lazy allocate entries slice on first match
			if entries == nil {
				entriesPtr = h.ipsetEntrySlicePool.Get().(*[]networking.IPSetEntry)
				entries = (*entriesPtr)[:0]
			}

			entries = h.processAddressRecord(reqMsg.Id, domain, v.AAAA, ttl, domainMatches, entries)

		case *dns.CNAME:
			domain := normalizeDomain(v.Hdr.Name)
			target := normalizeDomain(v.Target)
			ttl := v.Hdr.Ttl

			if log.IsVerbose() {
				log.Debugf("[%04x] CNAME record: %s -> %s (TTL: %d)", reqMsg.Id, domain, target, ttl)
			}

			// Add CNAME to cache
			cacheTTL := h.getRecordsCacheTTL(ttl, domain)
			h.recordsCache.AddAlias(domain, target, cacheTTL)
		}
	}

	// 4. Batch add to ipsets if we have any entries
	if len(entries) > 0 {
		if err := h.ipsetManager.BatchAddWithTTL(entries); err != nil {
			if !log.IsDisabled() {
				log.Warnf("[%04x] Failed to batch add to ipsets: %v", reqMsg.Id, err)
			}
		}

		// Return pooled slice
		for i := range entries {
			entries[i] = networking.IPSetEntry{}
		}
		*entriesPtr = entries
		h.ipsetEntrySlicePool.Put(entriesPtr)
	}
}

// processAddressRecord processes an A or AAAA record, adding it to cache and collecting IPSet entries if matched.
// domainMatches caches the matcher results for domains we've already checked in this response.
// entries is the pooled slice for collecting IPSet entries (may be nil if not yet allocated).
// Returns the updated entries slice.
func (h *ProxyHandler) processAddressRecord(
	msgID uint16,
	domain string,
	ip net.IP,
	originalTTL uint32,
	domainMatches map[string][]string,
	entries []networking.IPSetEntry,
) []networking.IPSetEntry {
	// Calculate cache TTL and add to cache
	cacheTTL := h.getRecordsCacheTTL(originalTTL, domain)
	isNew := h.recordsCache.AddAddress(domain, ip, cacheTTL)

	// If not new, skip IPSet processing
	if !isNew {
		return entries
	}

	// Check if we've already looked up this domain's matches
	ipsets, seen := domainMatches[domain]
	if !seen {
		// First time seeing this domain - check if it matches
		ipsets = h.matcher.Match(domain)
		domainMatches[domain] = ipsets // Cache result (even if empty)
	}

	// If domain doesn't match any ipsets, we're done
	if len(ipsets) == 0 {
		return entries
	}

	// Calculate IPSet TTL
	ipsetTTL := h.getIPSetTTL(originalTTL)
	isIPv4 := ip.To4() != nil

	// Create entries for all matching ipsets
	for _, ipsetName := range ipsets {
		ipsetCfg := h.ipsetsByName[ipsetName]
		if ipsetCfg == nil {
			continue
		}

		// Check IP version matches ipset
		if (isIPv4 && ipsetCfg.IPVersion != config.Ipv4) ||
			(!isIPv4 && ipsetCfg.IPVersion != config.Ipv6) {
			continue
		}

		// Convert to netip.Prefix
		addr, ok := netip.AddrFromSlice(ip)
		if !ok {
			if !log.IsDisabled() {
				log.Warnf("[%04x] Invalid IP address: %s", msgID, ip)
			}
			continue
		}

		prefixLen := ipv4PrefixLen
		if !isIPv4 {
			prefixLen = ipv6PrefixLen
		}
		prefix := netip.PrefixFrom(addr, prefixLen)

		if log.IsVerbose() {
			log.Infof("[%04x] Adding %s to ipset %s (domain: %s, TTL: %d)",
				msgID, ip, ipsetName, domain, ipsetTTL)
		}

		entries = append(entries, networking.IPSetEntry{
			IPSetName: ipsetName,
			Network:   prefix,
			TTL:       ipsetTTL,
		})
	}

	return entries
}

// getRecordsCacheTTL returns the TTL to use for the records cache for a domain.
// For domains matched by the matcher, uses ListedDomainsDNSCacheTTLSec if set (to make clients forget DNS fast).
// For other domains, uses the original DNS response TTL.
func (h *ProxyHandler) getRecordsCacheTTL(originalTTL uint32, domain string) uint32 {
	// Check if domain is in matcher (listed domain)
	matches := h.matcher.Match(domain)
	if len(matches) > 0 && h.config.ListedDomainsDNSCacheTTLSec > 0 {
		return h.config.ListedDomainsDNSCacheTTLSec
	}
	return originalTTL
}

// getIPSetTTL returns the TTL to use for IPSet entries.
// Uses original TTL plus IPSetEntryAdditionalTTLSec (e.g., original 300s + 7200s = 7500s total).
// If IPSetEntryAdditionalTTLSec is 0, returns the original TTL unchanged.
func (h *ProxyHandler) getIPSetTTL(originalTTL uint32) uint32 {
	if h.config.IPSetEntryAdditionalTTLSec == 0 {
		return originalTTL
	}
	return originalTTL + h.config.IPSetEntryAdditionalTTLSec
}

// cleanupLoop periodically cleans up expired cache entries.
func (h *ProxyHandler) cleanupLoop() {
	defer h.wg.Done()

	ticker := time.NewTicker(cacheCleanupInterval)
	defer ticker.Stop()

	for {
		select {
		case <-h.ctx.Done():
			return
		case <-ticker.C:
			h.recordsCache.EvictExpiredEntries()
		}
	}
}

// Shutdown cleans up handler resources.
func (h *ProxyHandler) Shutdown() {
	log.Infof("Shutting down DNS proxy handler...")

	// Cancel context to stop cleanup loop
	h.cancel()

	// Close all SSE subscribers
	h.CloseAllSubscribers()

	// Wait for cleanup loop to finish
	h.wg.Wait()

	// Close upstreams
	if h.upstream != nil {
		utils.CloseOrWarn(h.upstream)
	}
	for _, upstream := range h.ipsetUpstreams {
		utils.CloseOrWarn(upstream)
	}

	log.Infof("DNS proxy handler shut down")
}

// ReloadLists rebuilds the domain matcher from the current configuration.
// This should be called when lists are updated to pick up new domain entries.
func (h *ProxyHandler) ReloadLists() {
	log.Infof("Reloading DNS proxy domain lists...")
	h.recordsCache.Clear()
	h.matcher.Rebuild(h.appConfig)
	exactCount, wildcardCount := h.matcher.Stats()
	log.Infof("DNS proxy lists reloaded: %d exact domains, %d wildcard suffixes", exactCount, wildcardCount)
}

// MatchesIPSets returns the ipset names that match the given domain.
func (h *ProxyHandler) MatchesIPSets(domain string) []string {
	return h.matcher.Match(domain)
}

// GetDNSStrings returns the list of DNS server strings currently used by the proxy.
func (h *ProxyHandler) GetDNSStrings() []string {
	if h == nil || h.upstream == nil {
		return nil
	}
	return h.upstream.GetDNSStrings()
}

// ===== DNS Check Methods =====

// DNS check domain suffix for intercepting check requests
const dnsCheckDomain = "dns-check.keen-pbr.internal"

// DNS check response IP address
var dnsCheckResponseIP = net.ParseIP("8.8.8.8")

// Subscribe adds a new SSE subscriber for DNS check events.
// Returns a channel that will receive domain names when DNS check queries are received.
func (h *ProxyHandler) Subscribe() chan string {
	ch := make(chan string, 10)
	h.dnscheckSubscribersMu.Lock()
	h.dnscheckSubscribers[ch] = struct{}{}
	h.dnscheckSubscribersMu.Unlock()
	return ch
}

// Unsubscribe removes an SSE subscriber.
func (h *ProxyHandler) Unsubscribe(ch chan string) {
	h.dnscheckSubscribersMu.Lock()
	if _, exists := h.dnscheckSubscribers[ch]; exists {
		delete(h.dnscheckSubscribers, ch)
		close(ch)
	}
	h.dnscheckSubscribersMu.Unlock()
}

// CloseAllSubscribers closes all SSE subscriber channels.
// This should be called during shutdown to unblock any waiting readers.
func (h *ProxyHandler) CloseAllSubscribers() {
	h.dnscheckSubscribersMu.Lock()
	defer h.dnscheckSubscribersMu.Unlock()

	for ch := range h.dnscheckSubscribers {
		close(ch)
	}
	// Clear the map
	h.dnscheckSubscribers = make(map[chan string]struct{})
}

// broadcastDNSCheck broadcasts a domain to all SSE subscribers.
func (h *ProxyHandler) broadcastDNSCheck(domain string) {
	// Early exit if shutting down to prevent race with CloseAllSubscribers
	if h.ctx != nil && h.ctx.Err() != nil {
		return
	}

	h.dnscheckSubscribersMu.RLock()
	defer h.dnscheckSubscribersMu.RUnlock()

	for ch := range h.dnscheckSubscribers {
		select {
		case ch <- domain:
		default:
			// Channel full, skip
		}
	}
}

// isDNSCheckDomain checks if a domain matches the DNS check pattern.
// Returns true if domain is "dns-check.keen-pbr.internal" or "*.dns-check.keen-pbr.internal"
func isDNSCheckDomain(domain string) bool {
	if domain == dnsCheckDomain {
		return true
	}
	return strings.HasSuffix(domain, "."+dnsCheckDomain)
}

// createDNSCheckResponse creates a DNS response for DNS check queries.
func (h *ProxyHandler) createDNSCheckResponse(reqMsg *dns.Msg) *dns.Msg {
	respMsg := new(dns.Msg)
	respMsg.SetReply(reqMsg)
	respMsg.Authoritative = true

	if len(reqMsg.Question) > 0 {
		q := reqMsg.Question[0]
		if q.Qtype == dns.TypeA {
			rr := &dns.A{
				Hdr: dns.RR_Header{
					Name:   q.Name,
					Rrtype: dns.TypeA,
					Class:  dns.ClassINET,
					Ttl:    1, // Short TTL for check responses
				},
				A: dnsCheckResponseIP,
			}
			respMsg.Answer = append(respMsg.Answer, rr)
		}
	}

	return respMsg
}

// processDNSCheckRequest handles DNS check domain requests.
// Returns the response bytes if the domain matches, or nil if it doesn't match.
func (h *ProxyHandler) processDNSCheckRequest(reqMsg *dns.Msg) ([]byte, error) {
	if len(reqMsg.Question) == 0 {
		return nil, nil
	}

	domain := normalizeDomain(reqMsg.Question[0].Name)

	if !isDNSCheckDomain(domain) {
		return nil, nil
	}

	log.Debugf("[%04x] DNS check query intercepted: %s", reqMsg.Id, domain)

	// Broadcast to SSE subscribers
	h.broadcastDNSCheck(domain)

	// Create and return DNS check response
	respMsg := h.createDNSCheckResponse(reqMsg)
	respBytes, err := respMsg.Pack()
	if err != nil {
		return nil, fmt.Errorf("failed to pack DNS check response: %w", err)
	}

	return respBytes, nil
}

