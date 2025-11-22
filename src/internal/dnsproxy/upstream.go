package dnsproxy

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/miekg/dns"
)

// Upstream represents a DNS upstream resolver.
type Upstream interface {
	// Query sends a DNS query to the upstream and returns the response.
	Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error)
	// String returns a human-readable representation of the upstream.
	String() string
	// Close closes any resources held by the upstream.
	Close() error
}

// ParseUpstream parses an upstream URL and returns the appropriate Upstream implementation.
// Supported formats:
//   - keenetic:// - use Keenetic RCI to get DNS servers
//   - udp://ip:port - plain UDP DNS (port defaults to 53)
//   - doh://host/path - DNS-over-HTTPS
func ParseUpstream(upstreamURL string, keeneticClient domain.KeeneticClient) (Upstream, error) {
	if strings.HasPrefix(upstreamURL, "keenetic://") {
		return parseKeeneticUpstream(keeneticClient)
	}

	u, err := url.Parse(upstreamURL)
	// If url.Parse fails (e.g. "8.8.8.8:53"), or scheme is empty, try as UDP upstream
	if err != nil || u.Scheme == "" {
		return parseUDPUpstream(upstreamURL)
	}

	switch u.Scheme {
	case "udp":
		return parseUDPUpstream(u.Host)
	case "doh", "https":
		return NewDoHUpstream(upstreamURL), nil
	default:
		return nil, fmt.Errorf("unsupported upstream scheme: %s", u.Scheme)
	}
}

func parseKeeneticUpstream(keeneticClient domain.KeeneticClient) (Upstream, error) {
	if keeneticClient == nil {
		return nil, fmt.Errorf("keenetic:// upstream requires KeeneticClient")
	}
	return NewKeeneticUpstream(keeneticClient), nil
}

func parseUDPUpstream(address string) (Upstream, error) {
	host := address
	if !strings.Contains(host, ":") {
		host = net.JoinHostPort(host, "53")
	}

	// Validate address
	if _, _, err := net.SplitHostPort(host); err != nil {
		return nil, fmt.Errorf("invalid UDP address: %w", err)
	}

	return NewUDPUpstream(host), nil
}

// UDPUpstream implements Upstream using plain UDP DNS.
type UDPUpstream struct {
	address string
	client  *dns.Client
}

// NewUDPUpstream creates a new UDP DNS upstream.
func NewUDPUpstream(address string) *UDPUpstream {
	return &UDPUpstream{
		address: address,
		client: &dns.Client{
			Net:     "udp",
			Timeout: 5 * time.Second,
		},
	}
}

// Query sends a DNS query to the UDP upstream.
func (u *UDPUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	resp, _, err := u.client.ExchangeContext(ctx, req, u.address)
	if err != nil {
		return nil, fmt.Errorf("UDP query failed: %w", err)
	}
	return resp, nil
}

// String returns a human-readable representation of the upstream.
func (u *UDPUpstream) String() string {
	return fmt.Sprintf("udp://%s", u.address)
}

// Close closes any resources held by the upstream.
func (u *UDPUpstream) Close() error {
	return nil
}

// DoHUpstream implements Upstream using DNS-over-HTTPS.
type DoHUpstream struct {
	url    string
	client *http.Client
}

// NewDoHUpstream creates a new DNS-over-HTTPS upstream.
func NewDoHUpstream(urlStr string) *DoHUpstream {
	// Normalize URL scheme
	if strings.HasPrefix(urlStr, "doh://") {
		urlStr = "https://" + strings.TrimPrefix(urlStr, "doh://")
	}

	return &DoHUpstream{
		url: urlStr,
		client: &http.Client{
			Timeout: 10 * time.Second,
			Transport: &http.Transport{
				TLSClientConfig: &tls.Config{
					MinVersion: tls.VersionTLS12,
				},
				MaxIdleConns:        10,
				IdleConnTimeout:     30 * time.Second,
				DisableCompression:  true,
				MaxIdleConnsPerHost: 5,
			},
		},
	}
}

// Query sends a DNS query to the DoH upstream.
func (d *DoHUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	packed, err := req.Pack()
	if err != nil {
		return nil, fmt.Errorf("failed to pack DNS message: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, d.url, strings.NewReader(string(packed)))
	if err != nil {
		return nil, fmt.Errorf("failed to create HTTP request: %w", err)
	}

	httpReq.Header.Set("Content-Type", "application/dns-message")
	httpReq.Header.Set("Accept", "application/dns-message")

	resp, err := d.client.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("DoH request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("DoH request failed with status: %d", resp.StatusCode)
	}

	var body []byte
	buf := make([]byte, 4096)
	for {
		n, err := resp.Body.Read(buf)
		if n > 0 {
			body = append(body, buf[:n]...)
		}
		if err != nil {
			break
		}
	}

	dnsResp := new(dns.Msg)
	if err := dnsResp.Unpack(body); err != nil {
		return nil, fmt.Errorf("failed to unpack DNS response: %w", err)
	}

	return dnsResp, nil
}

// String returns a human-readable representation of the upstream.
func (d *DoHUpstream) String() string {
	return fmt.Sprintf("doh://%s", strings.TrimPrefix(d.url, "https://"))
}

// Close closes any resources held by the upstream.
func (d *DoHUpstream) Close() error {
	d.client.CloseIdleConnections()
	return nil
}

// KeeneticUpstream implements Upstream using Keenetic RCI to get DNS servers.
type KeeneticUpstream struct {
	keeneticClient domain.KeeneticClient

	mu        sync.RWMutex
	upstreams []Upstream
	lastFetch time.Time
	cacheTTL  time.Duration
}

// NewKeeneticUpstream creates a new Keenetic upstream.
func NewKeeneticUpstream(keeneticClient domain.KeeneticClient) *KeeneticUpstream {
	return &KeeneticUpstream{
		keeneticClient: keeneticClient,
		cacheTTL:       5 * time.Minute,
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
	return nil
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
	k.upstreams = make([]Upstream, 0, len(dnsServers))
	for _, server := range dnsServers {
		// Skip servers with specific domain (they're for specific zones)
		if server.Domain != nil {
			continue
		}

		upstream := createUpstreamFromDnsServerInfo(server)
		if upstream != nil {
			k.upstreams = append(k.upstreams, upstream)
		}
	}

	k.lastFetch = time.Now()
	log.Debugf("Fetched %d DNS servers from Keenetic", len(k.upstreams))

	return k.upstreams, nil
}

// createUpstreamFromDnsServerInfo creates an Upstream from a DnsServerInfo.
func createUpstreamFromDnsServerInfo(info keenetic.DnsServerInfo) Upstream {
	switch info.Type {
	case keenetic.DnsServerTypePlain, keenetic.DnsServerTypePlainIPv6:
		address := info.Proxy
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else if !strings.Contains(address, ":") || info.Type == keenetic.DnsServerTypePlainIPv6 {
			// For IPv4 without port or IPv6, add default port
			if info.Type == keenetic.DnsServerTypePlainIPv6 {
				address = net.JoinHostPort(address, "53")
			} else {
				address = net.JoinHostPort(address, "53")
			}
		}
		return NewUDPUpstream(address)

	case keenetic.DnsServerTypeDoT:
		// DoT: connect to local proxy that handles TLS
		var address string
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else {
			address = net.JoinHostPort(info.Proxy, "53")
		}
		return NewUDPUpstream(address)

	case keenetic.DnsServerTypeDoH:
		// DoH: connect to local proxy that handles HTTPS
		var address string
		if info.Port != "" {
			address = net.JoinHostPort(info.Proxy, info.Port)
		} else {
			address = net.JoinHostPort(info.Proxy, "53")
		}
		return NewUDPUpstream(address)

	default:
		log.Warnf("Unknown DNS server type: %s", info.Type)
		return nil
	}
}

// MultiUpstream wraps multiple upstreams and tries them in order.
type MultiUpstream struct {
	upstreams []Upstream
}

// NewMultiUpstream creates a new multi-upstream.
func NewMultiUpstream(upstreams []Upstream) *MultiUpstream {
	return &MultiUpstream{upstreams: upstreams}
}

// Query tries each upstream in order until one succeeds.
func (m *MultiUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	if len(m.upstreams) == 0 {
		return nil, fmt.Errorf("no upstreams configured")
	}

	var lastErr error
	for _, upstream := range m.upstreams {
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
