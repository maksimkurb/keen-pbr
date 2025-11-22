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
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/miekg/dns"
)

// ProxyConfig contains configuration for the DNS proxy.
type ProxyConfig struct {
	// ListenPort is the port to listen on (default: 15353)
	ListenPort uint16

	// ListenAddress is the address to listen on (default: 0.0.0.0)
	ListenAddress string

	// Upstreams is the list of upstream DNS URLs
	// Supported: keenetic://, udp://ip:port, doh://host/path
	Upstreams []string

	// DropAAAA drops AAAA (IPv6) responses (default: true)
	DropAAAA bool

	// TTLOverride overrides TTL in seconds (0 = use original)
	TTLOverride uint32
}

// DefaultProxyConfig returns the default proxy configuration.
func DefaultProxyConfig() ProxyConfig {
	return ProxyConfig{
		ListenPort:    15353,
		ListenAddress: "0.0.0.0",
		Upstreams:     []string{"keenetic://"},
		DropAAAA:      true,
		TTLOverride:   0,
	}
}

// ProxyConfigFromAppConfig creates a ProxyConfig from the application config.
func ProxyConfigFromAppConfig(cfg *config.Config) ProxyConfig {
	return ProxyConfig{
		ListenPort:    uint16(cfg.General.GetDNSProxyPort()),
		ListenAddress: "0.0.0.0",
		Upstreams:     cfg.General.GetDNSUpstream(),
		DropAAAA:      cfg.General.IsDropAAAAEnabled(),
		TTLOverride:   cfg.General.GetTTLOverride(),
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
	upstream Upstream

	// Per-ipset DNS overrides
	ipsetUpstreams map[string]Upstream

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
		ipsetUpstreams: make(map[string]Upstream),
		sseSubscribers: make(map[chan string]struct{}),
		ctx:            ctx,
		cancel:         cancel,
	}

	// Parse default upstream(s)
	var upstreams []Upstream
	for _, upstreamURL := range cfg.Upstreams {
		upstream, err := ParseUpstream(upstreamURL, keeneticClient)
		if err != nil {
			cancel()
			return nil, fmt.Errorf("failed to parse upstream %q: %w", upstreamURL, err)
		}
		upstreams = append(upstreams, upstream)
	}

	if len(upstreams) == 1 {
		proxy.upstream = upstreams[0]
	} else if len(upstreams) > 1 {
		proxy.upstream = NewMultiUpstream(upstreams)
	} else {
		cancel()
		return nil, fmt.Errorf("no upstreams configured")
	}

	// Parse per-ipset DNS overrides
	for _, ipset := range appConfig.IPSets {
		if ipset.Routing != nil && ipset.Routing.DNSOverride != "" {
			upstream, err := ParseUpstream(ipset.Routing.DNSOverride, keeneticClient)
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
	listenAddr := fmt.Sprintf("%s:%d", p.config.ListenAddress, p.config.ListenPort)

	// Start UDP listener
	udpAddr, err := net.ResolveUDPAddr("udp", listenAddr)
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %w", err)
	}

	p.udpConn, err = net.ListenUDP("udp", udpAddr)
	if err != nil {
		return fmt.Errorf("failed to listen UDP: %w", err)
	}

	// Start TCP listener
	p.tcpLn, err = net.Listen("tcp", listenAddr)
	if err != nil {
		p.udpConn.Close()
		return fmt.Errorf("failed to listen TCP: %w", err)
	}

	log.Infof("DNS proxy started on %s (UDP/TCP)", listenAddr)

	// Start listener goroutines
	p.wg.Add(2)
	go p.serveUDP()
	go p.serveTCP()

	// Start cleanup goroutine
	p.wg.Add(1)
	go p.cleanupLoop()

	return nil
}

// Stop stops the DNS proxy.
func (p *DNSProxy) Stop() error {
	log.Infof("Stopping DNS proxy...")
	p.cancel()

	// Close listeners
	if p.udpConn != nil {
		p.udpConn.Close()
	}
	if p.tcpLn != nil {
		p.tcpLn.Close()
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

// serveUDP handles incoming UDP DNS queries.
func (p *DNSProxy) serveUDP() {
	defer p.wg.Done()

	buf := make([]byte, dns.MaxMsgSize)

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
		}

		p.udpConn.SetReadDeadline(time.Now().Add(1 * time.Second))
		n, clientAddr, err := p.udpConn.ReadFromUDP(buf)
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

		go func(clientAddr *net.UDPAddr, req []byte) {
			resp, err := p.processRequest(clientAddr, req, "udp")
			if err != nil {
				log.Debugf("UDP request processing error: %v", err)
				return
			}

			_, err = p.udpConn.WriteToUDP(resp, clientAddr)
			if err != nil {
				log.Debugf("UDP write error: %v", err)
			}
		}(clientAddr, req)
	}
}

// serveTCP handles incoming TCP DNS queries.
func (p *DNSProxy) serveTCP() {
	defer p.wg.Done()

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
		}

		conn, err := p.tcpLn.Accept()
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

	conn.SetDeadline(time.Now().Add(10 * time.Second))

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
	resp, err := p.processRequest(conn.RemoteAddr(), req, "tcp")
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
	ctx, cancel := context.WithTimeout(p.ctx, 5*time.Second)
	defer cancel()

	respMsg, err := p.upstream.Query(ctx, &reqMsg)
	if err != nil {
		var netErr net.Error
		if errors.As(err, &netErr) && netErr.Timeout() {
			log.Warnf("[%04x] Upstream timeout", reqMsg.Id)
		} else {
			log.Debugf("[%04x] Upstream error: %v", reqMsg.Id, err)
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

	// Add to records cache
	p.recordsCache.AddAddress(domain, record.A, ttl)

	return p.collectIPSetEntries(domain, record.A, ttl, id)
}

// processAAAARecord processes an AAAA (IPv6) record and returns IPSet entries.
func (p *DNSProxy) processAAAARecord(record *dns.AAAA, id uint16) []networking.IPSetEntry {
	domain := normalizeDomain(record.Hdr.Name)
	ttl := p.getTTL(record.Hdr.Ttl)
	log.Debugf("[%04x] AAAA record: %s -> %s (TTL: %d)", id, domain, record.AAAA, ttl)

	// Add to records cache
	p.recordsCache.AddAddress(domain, record.AAAA, ttl)

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

			prefixLen := 32
			if !isIPv4 {
				prefixLen = 128
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

				prefixLen := 32
				if !isIPv4 {
					prefixLen = 128
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

	ticker := time.NewTicker(1 * time.Minute)
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
