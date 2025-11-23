package dnsproxy

import (
	"fmt"
	"net"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/miekg/dns"
)

// DNS check domain suffix for intercepting check requests
const dnsCheckDomain = "dns-check.keen-pbr.internal"

// DNS check response IP address
var dnsCheckResponseIP = net.ParseIP("192.168.255.255")

// Subscribe adds a new SSE subscriber for DNS check events.
// Returns a channel that will receive domain names when DNS check queries are received.
func (p *DNSProxy) Subscribe() chan string {
	ch := make(chan string, 10)
	p.sseSubscribersMu.Lock()
	p.sseSubscribers[ch] = struct{}{}
	p.sseSubscribersMu.Unlock()
	return ch
}

// Unsubscribe removes an SSE subscriber.
func (p *DNSProxy) Unsubscribe(ch chan string) {
	p.sseSubscribersMu.Lock()
	if _, exists := p.sseSubscribers[ch]; exists {
		delete(p.sseSubscribers, ch)
		close(ch)
	}
	p.sseSubscribersMu.Unlock()
}

// CloseAllSubscribers closes all SSE subscriber channels.
// This should be called during shutdown to unblock any waiting readers.
func (p *DNSProxy) CloseAllSubscribers() {
	p.sseSubscribersMu.Lock()
	defer p.sseSubscribersMu.Unlock()

	for ch := range p.sseSubscribers {
		close(ch)
	}
	// Clear the map
	p.sseSubscribers = make(map[chan string]struct{})
}

// broadcastDNSCheck broadcasts a domain to all SSE subscribers.
func (p *DNSProxy) broadcastDNSCheck(domain string) {
	p.sseSubscribersMu.RLock()
	defer p.sseSubscribersMu.RUnlock()

	for ch := range p.sseSubscribers {
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
	domain = strings.ToLower(domain)
	if domain == dnsCheckDomain {
		return true
	}
	return strings.HasSuffix(domain, "."+dnsCheckDomain)
}

// createDNSCheckResponse creates a DNS response with 192.168.255.255 for DNS check queries.
func (p *DNSProxy) createDNSCheckResponse(reqMsg *dns.Msg) *dns.Msg {
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
func (p *DNSProxy) processDNSCheckRequest(reqMsg *dns.Msg) ([]byte, error) {
	if len(reqMsg.Question) == 0 {
		return nil, nil
	}

	domain := normalizeDomain(reqMsg.Question[0].Name)

	if !isDNSCheckDomain(domain) {
		return nil, nil
	}

	log.Debugf("[%04x] DNS check query intercepted: %s", reqMsg.Id, domain)

	// Broadcast to SSE subscribers
	p.broadcastDNSCheck(domain)

	// Create and return DNS check response
	respMsg := p.createDNSCheckResponse(reqMsg)
	respBytes, err := respMsg.Pack()
	if err != nil {
		return nil, fmt.Errorf("failed to pack DNS check response: %w", err)
	}

	return respBytes, nil
}
