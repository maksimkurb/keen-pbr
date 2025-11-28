package dnsproxy

import (
	"context"
	"encoding/binary"
	"fmt"
	"net"
	"net/netip"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/upstreams"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/miekg/dns"
)

const (
	// Network protocol identifiers
	networkUDP = "udp"
	networkTCP = "tcp"

	// Timeout durations
	udpReadTimeout       = 3 * time.Second  // UDP read deadline - increased to handle packet bursts
	tcpConnectionTimeout = 15 * time.Second // TCP connection total timeout - increased for upstream+operations
	upstreamQueryTimeout = 10 * time.Second // Timeout for upstream DNS queries - should be longest

	// Cleanup intervals
	cacheCleanupInterval = 1 * time.Minute // How often to clean up expired cache entries

	// Size limits
	maxDNSMessageSize = 4096 // Maximum DNS message size for TCP to prevent DoS

	// IP address prefix lengths
	ipv4PrefixLen = 32  // /32 for single IPv4 addresses
	ipv6PrefixLen = 128 // /128 for single IPv6 addresses
)

// ProxyConfig contains configuration for the DNS proxy.
type ProxyConfig struct {
	// ListenAddr is the address to listen on (default: [::])
	ListenAddr string

	// ListenPort is the port to listen on (default: 15353)
	ListenPort uint16

	// Upstreams is the list of upstream DNS URLs
	// Supported: keenetic://, udp://ip:port, doh://host/path
	Upstreams []string

	// DropAAAA drops AAAA (IPv6) responses (default: true)
	DropAAAA bool

	// IPSetEntryAdditionalTTLSec is added to DNS record TTL to determine IPSet entry lifetime (default: 7200 = 2 hours)
	IPSetEntryAdditionalTTLSec uint32

	// ListedDomainsDNSCacheTTLSec is the TTL to use for domains found by matcher in recordsCache (default: 30)
	// This allows clients to forget DNS fast for domains that are in the matchers, while other domains keep original TTL
	ListedDomainsDNSCacheTTLSec uint32

	// MaxCacheDomains is the maximum number of domains to cache (default: 10000)
	MaxCacheDomains int
}

// ProxyConfigFromAppConfig creates a ProxyConfig from the application config.
func ProxyConfigFromAppConfig(cfg *config.Config) ProxyConfig {
	if cfg.General.DNSServer == nil {
		return ProxyConfig{}
	}
	return ProxyConfig{
		ListenAddr:                    cfg.General.DNSServer.ListenAddr,
		ListenPort:                    cfg.General.DNSServer.ListenPort,
		Upstreams:                     cfg.General.DNSServer.Upstreams,
		DropAAAA:                      cfg.General.DNSServer.DropAAAA,
		IPSetEntryAdditionalTTLSec:    cfg.General.DNSServer.IPSetEntryAdditionalTTLSec,
		ListedDomainsDNSCacheTTLSec:   cfg.General.DNSServer.ListedDomainsDNSCacheTTLSec,
		MaxCacheDomains:               cfg.General.DNSServer.CacheMaxDomains,
	}
}

// DNSProxy is a transparent DNS proxy that intercepts DNS traffic,
// forwards it to upstream resolvers, and processes responses to
// add resolved IPs to ipsets.
type DNSProxy struct {
	config ProxyConfig

	// Dependencies
	keeneticClient domain.KeeneticClient
	ipsetManager   domain.IPSetManager
	appConfig      *config.Config

	// Upstream resolver
	upstream upstreams.Upstream

	// Upstream providers (for API display of DNS servers)
	providers []upstreams.UpstreamProvider

	// Per-ipset DNS overrides
	ipsetUpstreams map[string]upstreams.Upstream

	// Domain matcher for routing decisions
	matcher *Matcher

	// Records cache for CNAME tracking
	recordsCache *RecordsCache

	// SSE broadcasting for DNS check
	sseSubscribersMu sync.RWMutex
	sseSubscribers   map[chan string]struct{}

	// Lifecycle
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup

	// Listeners
	udpConn *net.UDPConn
	tcpLn   net.Listener

	// Buffer pool for UDP requests
	bufferPool sync.Pool
}

// NewDNSProxy creates a new DNS proxy.
func NewDNSProxy(
	cfg ProxyConfig,
	keeneticClient domain.KeeneticClient,
	ipsetManager domain.IPSetManager,
	appConfig *config.Config,
) (*DNSProxy, error) {
	ctx, cancel := context.WithCancel(context.Background())

	maxCacheDomains := cfg.MaxCacheDomains
	if maxCacheDomains <= 0 {
		maxCacheDomains = 1000 // default - reduced for memory efficiency on embedded devices
	}

	proxy := &DNSProxy{
		config:         cfg,
		keeneticClient: keeneticClient,
		ipsetManager:   ipsetManager,
		appConfig:      appConfig,
		recordsCache:   NewRecordsCache(maxCacheDomains),
		ipsetUpstreams: make(map[string]upstreams.Upstream),
		sseSubscribers: make(map[chan string]struct{}),
		ctx:            ctx,
		cancel:         cancel,
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
		proxy.upstream = upstreamList[0]
	} else {
		// Multiple upstreams/providers or single provider
		proxy.upstream = upstreams.NewMultiUpstream(upstreamList, providerList)
	}

	// Store providers for API access
	proxy.providers = providerList

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
				proxy.ipsetUpstreams[ipset.IPSetName] = ipsetUpstream
				log.Infof("Configured DNS override for ipset %s: %s", ipset.IPSetName, strings.Join(ipsetUpstream.GetDNSStrings(), ", "))
			}
		}
	}

	// Create domain matcher
	proxy.matcher = NewMatcher(appConfig)

	// Initialize buffer pool for UDP requests
	proxy.bufferPool = sync.Pool{
		New: func() interface{} {
			buf := make([]byte, dns.MaxMsgSize)
			return &buf
		},
	}

	return proxy, nil
}

// Start starts the DNS proxy listeners.
func (p *DNSProxy) Start() error {
	// Use configured listen address (default: [::]:port for dual-stack)
	listenAddr := fmt.Sprintf("%s:%d", p.config.ListenAddr, p.config.ListenPort)

	udpAddr, err := net.ResolveUDPAddr("udp", listenAddr)
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %w", err)
	}

	p.udpConn, err = net.ListenUDP("udp", udpAddr)
	if err != nil {
		return fmt.Errorf("failed to listen UDP: %w", err)
	}

	p.tcpLn, err = net.Listen("tcp", listenAddr)
	if err != nil {
		utils.CloseOrWarn(p.udpConn)
		return fmt.Errorf("failed to listen TCP: %w", err)
	}

	log.Infof("DNS proxy started on %s (UDP/TCP) with upstream %v (cache: %d domains)", listenAddr, p.config.Upstreams, p.config.MaxCacheDomains)

	// Start listener goroutines
	p.wg.Add(2)
	go p.serveUDP(p.udpConn)
	go p.serveTCP(p.tcpLn)

	// Start cleanup goroutine
	p.wg.Add(1)
	go p.cleanupLoop()

	return nil
}

// Stop stops the DNS proxy.
func (p *DNSProxy) Stop() error {
	log.Infof("Stopping DNS proxy...")
	p.cancel()

	// Close all SSE subscribers first to unblock any waiting HTTP handlers
	p.CloseAllSubscribers()

	// Close listeners
	if p.udpConn != nil {
		utils.CloseOrWarn(p.udpConn)
	}
	if p.tcpLn != nil {
		utils.CloseOrWarn(p.tcpLn)
	}

	// Wait for goroutines
	p.wg.Wait()

	// Close upstreams
	if p.upstream != nil {
		utils.CloseOrWarn(p.upstream)
	}
	for _, upstream := range p.ipsetUpstreams {
		utils.CloseOrWarn(upstream)
	}

	log.Infof("DNS proxy stopped")
	return nil
}

// ReloadLists rebuilds the domain matcher from the current configuration.
// This should be called when lists are updated to pick up new domain entries.
func (p *DNSProxy) ReloadLists() {
	log.Infof("Reloading DNS proxy domain lists...")
	p.recordsCache.Cleanup()
	p.matcher.Rebuild(p.appConfig)
	exactCount, wildcardCount := p.matcher.Stats()
	log.Infof("DNS proxy lists reloaded: %d exact domains, %d wildcard suffixes", exactCount, wildcardCount)
}

// serveUDP handles incoming UDP DNS queries.
func (p *DNSProxy) serveUDP(conn *net.UDPConn) {
	defer p.wg.Done()

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
		}

		// Get buffer from pool
		bufPtr := p.bufferPool.Get().(*[]byte)
		buf := *bufPtr

		if err := conn.SetReadDeadline(time.Now().Add(udpReadTimeout)); err != nil {
			if log.IsVerbose() {
				log.Debugf("UDP set read deadline error: %v", err)
			}
		}
		n, clientAddr, err := conn.ReadFromUDP(buf)
		if err != nil {
			p.bufferPool.Put(bufPtr)
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if p.ctx.Err() != nil {
				return
			}
			if log.IsVerbose() {
				log.Debugf("UDP read error: %v", err)
			}
			continue
		}

		// Copy request data for goroutine
		req := make([]byte, n)
		copy(req, buf[:n])
		p.bufferPool.Put(bufPtr)

		go func(conn *net.UDPConn, clientAddr *net.UDPAddr, req []byte) {
			// Early exit if shutting down
			if p.ctx.Err() != nil {
				return
			}

			resp, err := p.processRequest(clientAddr, req, networkUDP)
			if err != nil {
				if log.IsVerbose() {
					log.Debugf("UDP request processing error: %v", err)
				}
				return
			}

			_, err = conn.WriteToUDP(resp, clientAddr)
			if err != nil {
				if !log.IsDisabled() {
					log.Warnf("UDP write error to %s: %v", clientAddr, err)
				}
			}
		}(conn, clientAddr, req)
	}
}

// serveTCP handles incoming TCP DNS queries.
func (p *DNSProxy) serveTCP(ln net.Listener) {
	defer p.wg.Done()

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
		}

		conn, err := ln.Accept()
		if err != nil {
			if p.ctx.Err() != nil {
				return
			}
			log.Debugf("TCP accept error: %v", err)
			continue
		}

		go p.handleTCPConnection(conn)
	}
}

// handleTCPConnection handles a single TCP DNS connection.
func (p *DNSProxy) handleTCPConnection(conn net.Conn) {
	defer utils.CloseOrWarn(conn)

	if err := conn.SetDeadline(time.Now().Add(tcpConnectionTimeout)); err != nil {
		log.Debugf("TCP set deadline error: %v", err)
		return
	}

	// Read length prefix
	var length uint16
	if err := binary.Read(conn, binary.BigEndian, &length); err != nil {
		log.Debugf("TCP read length error: %v", err)
		return
	}

	// Validate message size to prevent DoS
	if length == 0 || length > maxDNSMessageSize {
		log.Warnf("Invalid TCP DNS message length: %d from %s", length, conn.RemoteAddr())
		return
	}

	// Read DNS message
	req := make([]byte, length)
	if _, err := conn.Read(req); err != nil {
		log.Debugf("TCP read message error: %v", err)
		return
	}

	// Process request
	resp, err := p.processRequest(conn.RemoteAddr(), req, networkTCP)
	if err != nil {
		log.Debugf("TCP request processing error: %v", err)
		return
	}

	// Write length prefix
	if err := binary.Write(conn, binary.BigEndian, uint16(len(resp))); err != nil {
		log.Debugf("TCP write length error: %v", err)
		return
	}

	// Write response
	if _, err := conn.Write(resp); err != nil {
		log.Debugf("TCP write response error: %v", err)
	}
}

// processRequest processes a DNS request and returns the response.
func (p *DNSProxy) processRequest(clientAddr net.Addr, reqBytes []byte, network string) ([]byte, error) {
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
	if respBytes, err := p.processDNSCheckRequest(&reqMsg); respBytes != nil || err != nil {
		return respBytes, err
	}

	// Select upstream: check ipset-specific overrides first, then use default
	selectedUpstream := p.upstream // Default upstream

	// Check if any ipset has DNS override matching this query domain
	if len(reqMsg.Question) > 0 {
		queryDomain := reqMsg.Question[0].Name

		for ipsetName, ipsetUpstream := range p.ipsetUpstreams {
			if ipsetUpstream.MatchesDomain(queryDomain) {
				if log.IsVerbose() {
					log.Debugf("[%04x] Using ipset-specific DNS for %s (ipset: %s)", reqMsg.Id, queryDomain, ipsetName)
				}
				selectedUpstream = ipsetUpstream
				break
			}
		}
	}

	// Forward to selected upstream
	ctx, cancel := context.WithTimeout(p.ctx, upstreamQueryTimeout)
	defer cancel()

	respMsg, err := selectedUpstream.Query(ctx, &reqMsg)
	if err != nil {
		return nil, err
	}

	// Process response (filter AAAA, match domains, add to ipsets)
	p.processResponse(&reqMsg, respMsg)

	// Pack response
	respBytes, err := respMsg.Pack()
	if err != nil {
		return nil, fmt.Errorf("failed to pack response: %w", err)
	}

	return respBytes, nil
}

// processResponse processes a DNS response, filtering AAAA records if configured
// and adding resolved IPs to ipsets.
func (p *DNSProxy) processResponse(reqMsg, respMsg *dns.Msg) {
	// Filter AAAA records if configured
	if p.config.DropAAAA {
		var filtered []dns.RR
		for _, rr := range respMsg.Answer {
			if _, ok := rr.(*dns.AAAA); !ok {
				filtered = append(filtered, rr)
			}
		}
		respMsg.Answer = filtered
	}

	// Skip if no successful response
	if respMsg.Rcode != dns.RcodeSuccess {
		return
	}

	var entries []networking.IPSetEntry

	// Process each answer record
	for _, rr := range respMsg.Answer {
		if rr == nil {
			continue
		}

		switch v := rr.(type) {
		case *dns.A:
			entries = append(entries, p.processARecord(v, reqMsg.Id)...)
		case *dns.AAAA:
			entries = append(entries, p.processAAAARecord(v, reqMsg.Id)...)
		case *dns.CNAME:
			entries = append(entries, p.processCNAMERecord(v, reqMsg.Id)...)
		}
	}

	// Batch add to ipsets
	if len(entries) > 0 {
		if err := p.ipsetManager.BatchAddWithTTL(entries); err != nil {
			if !log.IsDisabled() {
				log.Warnf("[%04x] Failed to batch add to ipsets: %v", reqMsg.Id, err)
			}
		}
	}
}

// processARecord processes an A (IPv4) record and returns IPSet entries.
func (p *DNSProxy) processARecord(record *dns.A, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	originalTTL := record.Hdr.Ttl
	cacheTTL := p.getRecordsCacheTTL(originalTTL, domain)

	if log.IsVerbose() {
		log.Debugf("[%04x] A record: %s -> %s (TTL: %d)", id, domain, record.A, originalTTL)
	}

	// Add to records cache - only collect ipset entries if this is a new/expired entry
	if !p.recordsCache.AddAddress(domain, record.A, cacheTTL) {
		return nil // Entry already cached and valid, skip ipset update
	}

	return p.collectIPSetEntries(domain, record.A, originalTTL, id)
}

// processAAAARecord processes an AAAA (IPv6) record and returns IPSet entries.
func (p *DNSProxy) processAAAARecord(record *dns.AAAA, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	originalTTL := record.Hdr.Ttl
	cacheTTL := p.getRecordsCacheTTL(originalTTL, domain)

	if log.IsVerbose() {
		log.Debugf("[%04x] AAAA record: %s -> %s (TTL: %d)", id, domain, record.AAAA, originalTTL)
	}

	// Add to records cache - only collect ipset entries if this is a new/expired entry
	if !p.recordsCache.AddAddress(domain, record.AAAA, cacheTTL) {
		return nil // Entry already cached and valid, skip ipset update
	}

	return p.collectIPSetEntries(domain, record.AAAA, originalTTL, id)
}

// collectIPSetEntries checks domain aliases against lists and returns IPSet entries.
// originalTTL parameter is the original DNS response TTL.
func (p *DNSProxy) collectIPSetEntries(domain string, ip net.IP, originalTTL uint32, id uint16) []networking.IPSetEntry {
	// Quick check: if domain itself doesn't match, skip aliases lookup
	// This avoids GetAliases() allocation for non-matching domains
	quickMatches := p.matcher.Match(domain)
	if len(quickMatches) == 0 {
		return nil // No match, return nil to avoid allocation
	}

	ipsetTTL := p.getIPSetTTL(originalTTL)

	// Domain matches, now check all aliases
	aliases := p.recordsCache.GetAliases(domain)
	var entries []networking.IPSetEntry

	for _, alias := range aliases {
		// Cache the address for this alias to prevent duplicate ipset additions
		// when CNAME record is processed later (or on subsequent lookups)
		if alias != domain {
			aliasCacheTTL := p.getRecordsCacheTTL(originalTTL, alias)
			p.recordsCache.AddAddress(alias, ip, aliasCacheTTL)
		}

		matches := p.matcher.Match(alias)
		for _, ipsetName := range matches {
			ipsetCfg := p.matcher.GetIPSet(ipsetName)
			if ipsetCfg == nil {
				continue
			}

			// Check IP version
			isIPv4 := ip.To4() != nil
			if (isIPv4 && ipsetCfg.IPVersion != config.Ipv4) ||
				(!isIPv4 && ipsetCfg.IPVersion != config.Ipv6) {
				continue
			}

			// Convert to netip.Prefix
			addr, ok := netip.AddrFromSlice(ip)
			if !ok {
				if !log.IsDisabled() {
					log.Warnf("[%04x] Invalid IP address: %s", id, ip)
				}
				continue
			}

			prefixLen := ipv4PrefixLen
			if !isIPv4 {
				prefixLen = ipv6PrefixLen
			}
			prefix := netip.PrefixFrom(addr, prefixLen)

			if log.IsVerbose() {
				log.Infof("[%04x] Adding %s to ipset %s (domain: %s, alias: %s, TTL: %d)",
					id, ip, ipsetName, domain, alias, ipsetTTL)
			}

			entries = append(entries, networking.IPSetEntry{
				IPSetName: ipsetName,
				Network:   prefix,
				TTL:       ipsetTTL,
			})
		}
	}
	return entries
}

// processCNAMERecord processes a CNAME record and returns IPSet entries.
func (p *DNSProxy) processCNAMERecord(record *dns.CNAME, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	target := normalizeDomain(record.Target)
	originalTTL := record.Hdr.Ttl
	cacheTTL := p.getRecordsCacheTTL(originalTTL, domain)
	ipsetTTL := p.getIPSetTTL(originalTTL)

	if log.IsVerbose() {
		log.Debugf("[%04x] CNAME record: %s -> %s (TTL: %d)", id, domain, target, originalTTL)
	}

	// Add alias to cache
	p.recordsCache.AddAlias(domain, target, cacheTTL)

	// Check if we already have addresses for the target
	addresses := p.recordsCache.GetAddresses(target)
	if len(addresses) == 0 {
		return nil
	}

	aliases := p.recordsCache.GetAliases(domain)
	var entries []networking.IPSetEntry

	for _, alias := range aliases {
		matches := p.matcher.Match(alias)
		for _, ipsetName := range matches {
			ipsetCfg := p.matcher.GetIPSet(ipsetName)
			if ipsetCfg == nil {
				continue
			}

			for _, cachedAddr := range addresses {
				// Check if this domain+IP combination is already in cache and valid
				// This prevents duplicate ipset additions for the same CNAME resolution
				if !p.recordsCache.AddAddress(domain, cachedAddr.Address, cacheTTL) {
					continue // Already cached and valid, skip ipset update
				}

				// Check IP version matches ipset
				isIPv4 := cachedAddr.Address.To4() != nil
				if (isIPv4 && ipsetCfg.IPVersion != config.Ipv4) ||
					(!isIPv4 && ipsetCfg.IPVersion != config.Ipv6) {
					continue
				}

				// Convert to netip.Prefix
				addr, ok := netip.AddrFromSlice(cachedAddr.Address)
				if !ok {
					continue
				}

				prefixLen := ipv4PrefixLen
				if !isIPv4 {
					prefixLen = ipv6PrefixLen
				}
				prefix := netip.PrefixFrom(addr, prefixLen)

				if log.IsVerbose() {
					log.Infof("[%04x] Adding %s to ipset %s (CNAME: %s -> %s, alias: %s, TTL: %d)",
						id, cachedAddr.Address, ipsetName, domain, target, alias, ipsetTTL)
				}

				entries = append(entries, networking.IPSetEntry{
					IPSetName: ipsetName,
					Network:   prefix,
					TTL:       ipsetTTL,
				})
			}
		}
	}
	return entries
}

// getRecordsCacheTTL returns the TTL to use for the records cache for a domain.
// For domains matched by the matcher, uses ListedDomainsDNSCacheTTLSec if set (to make clients forget DNS fast).
// For other domains, uses the original DNS response TTL.
func (p *DNSProxy) getRecordsCacheTTL(originalTTL uint32, domain string) uint32 {
	// Check if domain is in matcher (listed domain)
	matches := p.matcher.Match(domain)
	if len(matches) > 0 && p.config.ListedDomainsDNSCacheTTLSec > 0 {
		return p.config.ListedDomainsDNSCacheTTLSec
	}
	return originalTTL
}

// getIPSetTTL returns the TTL to use for IPSet entries.
// Uses original TTL plus IPSetEntryAdditionalTTLSec (e.g., original 300s + 7200s = 7500s total).
// If IPSetEntryAdditionalTTLSec is 0, returns the original TTL unchanged.
func (p *DNSProxy) getIPSetTTL(originalTTL uint32) uint32 {
	if p.config.IPSetEntryAdditionalTTLSec == 0 {
		return originalTTL
	}
	return originalTTL + p.config.IPSetEntryAdditionalTTLSec
}

// cleanupLoop periodically cleans up expired cache entries.
func (p *DNSProxy) cleanupLoop() {
	defer p.wg.Done()

	ticker := time.NewTicker(cacheCleanupInterval)
	defer ticker.Stop()

	for {
		select {
		case <-p.ctx.Done():
			return
		case <-ticker.C:
			p.recordsCache.Cleanup()
		}
	}
}

// normalizeDomain removes the trailing dot from a domain name.
func normalizeDomain(domain string) string {
	if len(domain) > 0 && domain[len(domain)-1] == '.' {
		return domain[:len(domain)-1]
	}
	return domain
}

// GetDNSStrings returns the list of DNS server strings currently used by the proxy.
func (p *DNSProxy) GetDNSStrings() []string {
	return p.upstream.GetDNSStrings()
}
