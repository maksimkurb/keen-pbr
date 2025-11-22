package dnsproxy

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/upstreams"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/miekg/dns"
)

const (
	// Network protocol identifiers
	networkUDP = "udp"
	networkTCP = "tcp"

	// Timeout durations
	udpReadTimeout       = 1 * time.Second  // UDP read deadline for non-blocking accept loop
	tcpConnectionTimeout = 10 * time.Second // TCP connection total timeout
	upstreamQueryTimeout = 5 * time.Second  // Timeout for upstream DNS queries

	// Cleanup intervals
	cacheCleanupInterval = 1 * time.Minute // How often to clean up expired cache entries

	// IP address prefix lengths
	ipv4PrefixLen = 32  // /32 for single IPv4 addresses
	ipv6PrefixLen = 128 // /128 for single IPv6 addresses
)

// ProxyConfig contains configuration for the DNS proxy.
type ProxyConfig struct {
	// ListenPort is the port to listen on (default: 15353)
	ListenPort uint16

	// ListenAddress is the IPv4 address to listen on (default: 127.0.53.53)
	ListenAddress string

	// ListenAddressIPv6 is the IPv6 address to listen on (default: fd53::53)
	ListenAddressIPv6 string

	// Upstreams is the list of upstream DNS URLs
	// Supported: keenetic://, udp://ip:port, doh://host/path
	Upstreams []string

	// DropAAAA drops AAAA (IPv6) responses (default: true)
	DropAAAA bool

	// TTLOverride overrides TTL in seconds (0 = use original)
	TTLOverride uint32
}

// ProxyConfigFromAppConfig creates a ProxyConfig from the application config.
func ProxyConfigFromAppConfig(cfg *config.Config) ProxyConfig {
	return ProxyConfig{
		ListenPort:        uint16(cfg.General.GetDNSProxyPort()),
		ListenAddress:     cfg.General.GetDNSProxyHost(),
		ListenAddressIPv6: cfg.General.GetDNSProxyHostIPv6(),
		Upstreams:         cfg.General.GetDNSUpstream(),
		DropAAAA:          cfg.General.IsDropAAAAEnabled(),
		TTLOverride:       cfg.General.GetTTLOverride(),
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

	// Listeners (IPv4)
	udpConn *net.UDPConn
	tcpLn   net.Listener

	// Listeners (IPv6)
	udpConn6 *net.UDPConn
	tcpLn6   net.Listener
}

// NewDNSProxy creates a new DNS proxy.
func NewDNSProxy(
	cfg ProxyConfig,
	keeneticClient domain.KeeneticClient,
	ipsetManager domain.IPSetManager,
	appConfig *config.Config,
) (*DNSProxy, error) {
	ctx, cancel := context.WithCancel(context.Background())

	proxy := &DNSProxy{
		config:         cfg,
		keeneticClient: keeneticClient,
		ipsetManager:   ipsetManager,
		appConfig:      appConfig,
		recordsCache:   NewRecordsCache(),
		ipsetUpstreams: make(map[string]upstreams.Upstream),
		sseSubscribers: make(map[chan string]struct{}),
		ctx:            ctx,
		cancel:         cancel,
	}

	// Parse default upstream(s)
	var upstreamList []upstreams.Upstream
	for _, upstreamURL := range cfg.Upstreams {
		upstream, err := upstreams.ParseUpstream(upstreamURL, keeneticClient, "")
		if err != nil {
			cancel()
			return nil, fmt.Errorf("failed to parse upstream %q: %w", upstreamURL, err)
		}
		upstreamList = append(upstreamList, upstream)
	}

	if len(upstreamList) == 1 {
		proxy.upstream = upstreamList[0]
	} else if len(upstreamList) > 1 {
		proxy.upstream = upstreams.NewMultiUpstream(upstreamList)
	} else {
		cancel()
		return nil, fmt.Errorf("no upstreams configured")
	}

	// Parse per-ipset DNS overrides
	for _, ipset := range appConfig.IPSets {
		if ipset.Routing != nil && ipset.Routing.DNSOverride != "" {
			upstream, err := upstreams.ParseUpstream(ipset.Routing.DNSOverride, keeneticClient, "")
			if err != nil {
				log.Warnf("Failed to parse DNS override for ipset %s: %v", ipset.IPSetName, err)
				continue
			}
			proxy.ipsetUpstreams[ipset.IPSetName] = upstream
		}
	}

	// Create domain matcher
	proxy.matcher = NewMatcher(appConfig)

	return proxy, nil
}

// Start starts the DNS proxy listeners.
func (p *DNSProxy) Start() error {
	// Start IPv4 listeners
	listenAddr := fmt.Sprintf("%s:%d", p.config.ListenAddress, p.config.ListenPort)

	udpAddr, err := net.ResolveUDPAddr("udp4", listenAddr)
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %w", err)
	}

	p.udpConn, err = net.ListenUDP("udp4", udpAddr)
	if err != nil {
		return fmt.Errorf("failed to listen UDP: %w", err)
	}

	p.tcpLn, err = net.Listen("tcp4", listenAddr)
	if err != nil {
		p.udpConn.Close()
		return fmt.Errorf("failed to listen TCP: %w", err)
	}

	log.Infof("DNS proxy started on %s (UDP/TCP)", listenAddr)

	// Start IPv6 listeners (optional, don't fail if unavailable)
	if p.config.ListenAddressIPv6 != "" {
		listenAddr6 := fmt.Sprintf("[%s]:%d", p.config.ListenAddressIPv6, p.config.ListenPort)

		udpAddr6, err := net.ResolveUDPAddr("udp6", listenAddr6)
		if err != nil {
			log.Warnf("Failed to resolve IPv6 UDP address: %v", err)
		} else {
			p.udpConn6, err = net.ListenUDP("udp6", udpAddr6)
			if err != nil {
				log.Warnf("Failed to listen on IPv6 UDP: %v", err)
			}
		}

		if p.udpConn6 != nil {
			p.tcpLn6, err = net.Listen("tcp6", listenAddr6)
			if err != nil {
				log.Warnf("Failed to listen on IPv6 TCP: %v", err)
				p.udpConn6.Close()
				p.udpConn6 = nil
			} else {
				log.Infof("DNS proxy started on %s (UDP/TCP)", listenAddr6)
			}
		}
	}

	// Start listener goroutines for IPv4
	p.wg.Add(2)
	go p.serveUDP(p.udpConn)
	go p.serveTCP(p.tcpLn)

	// Start listener goroutines for IPv6 if available
	if p.udpConn6 != nil && p.tcpLn6 != nil {
		p.wg.Add(2)
		go p.serveUDP(p.udpConn6)
		go p.serveTCP(p.tcpLn6)
	}

	// Start cleanup goroutine
	p.wg.Add(1)
	go p.cleanupLoop()

	return nil
}

// Stop stops the DNS proxy.
func (p *DNSProxy) Stop() error {
	log.Infof("Stopping DNS proxy...")
	p.cancel()

	// Close IPv4 listeners
	if p.udpConn != nil {
		p.udpConn.Close()
	}
	if p.tcpLn != nil {
		p.tcpLn.Close()
	}

	// Close IPv6 listeners
	if p.udpConn6 != nil {
		p.udpConn6.Close()
	}
	if p.tcpLn6 != nil {
		p.tcpLn6.Close()
	}

	// Wait for goroutines
	p.wg.Wait()

	// Close upstreams
	if p.upstream != nil {
		p.upstream.Close()
	}
	for _, upstream := range p.ipsetUpstreams {
		upstream.Close()
	}

	log.Infof("DNS proxy stopped")
	return nil
}

// ReloadLists rebuilds the domain matcher from the current configuration.
// This should be called when lists are updated to pick up new domain entries.
func (p *DNSProxy) ReloadLists() {
	log.Infof("Reloading DNS proxy domain lists...")
	p.matcher.Rebuild(p.appConfig)
	exactCount, wildcardCount := p.matcher.Stats()
	log.Infof("DNS proxy lists reloaded: %d exact domains, %d wildcard suffixes", exactCount, wildcardCount)
}

// serveUDP handles incoming UDP DNS queries.
func (p *DNSProxy) serveUDP(conn *net.UDPConn) {
	defer p.wg.Done()

	buf := make([]byte, dns.MaxMsgSize)

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
		}

		conn.SetReadDeadline(time.Now().Add(udpReadTimeout))
		n, clientAddr, err := conn.ReadFromUDP(buf)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if p.ctx.Err() != nil {
				return
			}
			log.Debugf("UDP read error: %v", err)
			continue
		}

		// Handle request in goroutine
		req := make([]byte, n)
		copy(req, buf[:n])

		go func(conn *net.UDPConn, clientAddr *net.UDPAddr, req []byte) {
			resp, err := p.processRequest(clientAddr, req, networkUDP)
			if err != nil {
				log.Debugf("UDP request processing error: %v", err)
				return
			}

			_, err = conn.WriteToUDP(resp, clientAddr)
			if err != nil {
				log.Debugf("UDP write error: %v", err)
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
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(tcpConnectionTimeout))

	// Read length prefix
	var length uint16
	if err := binary.Read(conn, binary.BigEndian, &length); err != nil {
		log.Debugf("TCP read length error: %v", err)
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
	if len(reqMsg.Question) > 0 {
		q := reqMsg.Question[0]
		log.Debugf("[%04x] DNS query: %s %s from %s via %s",
			reqMsg.Id, q.Name, dns.TypeToString[q.Qtype], clientAddr, network)
	}

	// Check if this is a DNS check domain - intercept and respond immediately
	if respBytes, err := p.processDNSCheckRequest(&reqMsg); respBytes != nil || err != nil {
		return respBytes, err
	}

	// Forward to upstream
	ctx, cancel := context.WithTimeout(p.ctx, upstreamQueryTimeout)
	defer cancel()

	log.Debugf("[%04x] Querying upstream: %s", reqMsg.Id, p.upstream.String())
	respMsg, err := p.upstream.Query(ctx, &reqMsg)
	if err != nil {
		// Extract query info for better error logging
		queryInfo := "unknown"
		if len(reqMsg.Question) > 0 {
			q := reqMsg.Question[0]
			queryInfo = fmt.Sprintf("%s %s", q.Name, dns.TypeToString[q.Qtype])
		}

		// Check if it's a context timeout vs network timeout
		if ctx.Err() == context.DeadlineExceeded {
			log.Warnf("[%04x] Context deadline exceeded for query: %s (upstream: %s)", reqMsg.Id, queryInfo, p.upstream.String())
		} else if errors.Is(err, context.DeadlineExceeded) {
			log.Warnf("[%04x] Upstream timeout (context) for query: %s (upstream: %s)", reqMsg.Id, queryInfo, p.upstream.String())
		} else {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				log.Warnf("[%04x] Upstream timeout (network) for query: %s (upstream: %s)", reqMsg.Id, queryInfo, p.upstream.String())
			} else {
				log.Debugf("[%04x] Upstream error for query %s (upstream: %s): %v", reqMsg.Id, queryInfo, p.upstream.String(), err)
			}
		}
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
		if err := networking.BatchAddWithTTL(entries); err != nil {
			log.Warnf("[%04x] Failed to batch add to ipsets: %v", reqMsg.Id, err)
		}
	}
}

// processARecord processes an A (IPv4) record and returns IPSet entries.
func (p *DNSProxy) processARecord(record *dns.A, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	ttl := p.getTTL(record.Hdr.Ttl)
	log.Debugf("[%04x] A record: %s -> %s (TTL: %d)", id, domain, record.A, ttl)

	// Add to records cache - only collect ipset entries if this is a new/expired entry
	if !p.recordsCache.AddAddress(domain, record.A, ttl) {
		return nil // Entry already cached and valid, skip ipset update
	}

	return p.collectIPSetEntries(domain, record.A, ttl, id)
}

// processAAAARecord processes an AAAA (IPv6) record and returns IPSet entries.
func (p *DNSProxy) processAAAARecord(record *dns.AAAA, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	ttl := p.getTTL(record.Hdr.Ttl)
	log.Debugf("[%04x] AAAA record: %s -> %s (TTL: %d)", id, domain, record.AAAA, ttl)

	// Add to records cache - only collect ipset entries if this is a new/expired entry
	if !p.recordsCache.AddAddress(domain, record.AAAA, ttl) {
		return nil // Entry already cached and valid, skip ipset update
	}

	return p.collectIPSetEntries(domain, record.AAAA, ttl, id)
}

// collectIPSetEntries checks domain aliases against lists and returns IPSet entries.
func (p *DNSProxy) collectIPSetEntries(domain string, ip net.IP, ttl uint32, id uint16) []networking.IPSetEntry {
	var entries []networking.IPSetEntry
	aliases := p.recordsCache.GetAliases(domain)

	for _, alias := range aliases {
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
				log.Warnf("[%04x] Invalid IP address: %s", id, ip)
				continue
			}

			prefixLen := ipv4PrefixLen
			if !isIPv4 {
				prefixLen = ipv6PrefixLen
			}
			prefix := netip.PrefixFrom(addr, prefixLen)

			log.Infof("[%04x] Adding %s to ipset %s (domain: %s, alias: %s, TTL: %d)",
				id, ip, ipsetName, domain, alias, ttl)

			entries = append(entries, networking.IPSetEntry{
				IPSetName: ipsetName,
				Network:   prefix,
				TTL:       ttl,
			})
		}
	}
	return entries
}

// processCNAMERecord processes a CNAME record and returns IPSet entries.
func (p *DNSProxy) processCNAMERecord(record *dns.CNAME, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	target := normalizeDomain(record.Target)
	ttl := p.getTTL(record.Hdr.Ttl)

	log.Debugf("[%04x] CNAME record: %s -> %s (TTL: %d)", id, domain, target, ttl)

	// Add alias to cache
	p.recordsCache.AddAlias(domain, target, ttl)

	// Check if we already have addresses for the target
	addresses := p.recordsCache.GetAddresses(target)
	if len(addresses) == 0 {
		return nil
	}

	var entries []networking.IPSetEntry
	aliases := p.recordsCache.GetAliases(domain)

	for _, alias := range aliases {
		matches := p.matcher.Match(alias)
		for _, ipsetName := range matches {
			ipsetCfg := p.matcher.GetIPSet(ipsetName)
			if ipsetCfg == nil {
				continue
			}

			for _, cachedAddr := range addresses {
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

				log.Infof("[%04x] Adding %s to ipset %s (CNAME: %s -> %s, alias: %s, TTL: %d)",
					id, cachedAddr.Address, ipsetName, domain, target, alias, ttl)

				entries = append(entries, networking.IPSetEntry{
					IPSetName: ipsetName,
					Network:   prefix,
					TTL:       ttl,
				})
			}
		}
	}
	return entries
}

// getTTL returns the TTL to use, applying override if configured.
func (p *DNSProxy) getTTL(originalTTL uint32) uint32 {
	if p.config.TTLOverride > 0 {
		return p.config.TTLOverride
	}
	return originalTTL
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

// GetStats returns DNS proxy statistics.
func (p *DNSProxy) GetStats() map[string]interface{} {
	addressCount, aliasCount := p.recordsCache.Stats()
	return map[string]interface{}{
		"listen_port":   p.config.ListenPort,
		"upstream":      p.upstream.String(),
		"drop_aaaa":     p.config.DropAAAA,
		"ttl_override":  p.config.TTLOverride,
		"cached_addrs":  addressCount,
		"cached_cnames": aliasCount,
	}
}

// GetDNSServers returns the list of DNS servers currently used by the proxy.
func (p *DNSProxy) GetDNSServers() []keenetic.DNSServerInfo {
	if p.upstream == nil {
		return nil
	}
	return p.upstream.GetDNSServers()
}
