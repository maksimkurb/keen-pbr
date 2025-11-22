package upstreams

import (
	"context"
	"crypto/tls"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/miekg/dns"
)

const (
	// URL scheme constants
	dohScheme   = "doh://"
	httpsScheme = "https://"

	// HTTP client configuration
	dohClientTimeout       = 10 * time.Second // Total timeout for DoH requests
	dohIdleConnTimeout     = 30 * time.Second // How long idle connections are kept
	dohMaxIdleConns        = 10               // Maximum idle connections total
	dohMaxIdleConnsPerHost = 5                // Maximum idle connections per host

	// HTTP content types
	dnsMessageContentType = "application/dns-message"

	// Buffer sizes
	dohReadBufferSize = 4096 // Buffer size for reading DoH responses
)

// DoHUpstream implements Upstream using DNS-over-HTTPS.
type DoHUpstream struct {
	BaseUpstream
	url    string
	client *http.Client
}

// NewDoHUpstream creates a new DNS-over-HTTPS upstream.
// The domain parameter restricts the upstream to a specific domain (empty = all domains).
func NewDoHUpstream(urlStr string, restrictedDomain string) *DoHUpstream {
	// Normalize URL scheme
	if strings.HasPrefix(urlStr, dohScheme) {
		urlStr = httpsScheme + strings.TrimPrefix(urlStr, dohScheme)
	}

	return &DoHUpstream{
		BaseUpstream: BaseUpstream{Domain: restrictedDomain},
		url:          urlStr,
		client: &http.Client{
			Timeout: dohClientTimeout,
			Transport: &http.Transport{
				TLSClientConfig: &tls.Config{
					MinVersion: tls.VersionTLS12,
				},
				MaxIdleConns:        dohMaxIdleConns,
				IdleConnTimeout:     dohIdleConnTimeout,
				DisableCompression:  true,
				MaxIdleConnsPerHost: dohMaxIdleConnsPerHost,
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

	httpReq.Header.Set("Content-Type", dnsMessageContentType)
	httpReq.Header.Set("Accept", dnsMessageContentType)

	resp, err := d.client.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("DoH request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("DoH request failed with status: %d", resp.StatusCode)
	}

	var body []byte
	buf := make([]byte, dohReadBufferSize)
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
	displayURL := strings.TrimPrefix(d.url, httpsScheme)
	if d.Domain != "" {
		return fmt.Sprintf("doh://%s (domain: %s)", displayURL, d.Domain)
	}
	return fmt.Sprintf("doh://%s", displayURL)
}

// Close closes any resources held by the upstream.
func (d *DoHUpstream) Close() error {
	d.client.CloseIdleConnections()
	return nil
}

// GetDNSServers returns the DNS server info for this upstream.
func (d *DoHUpstream) GetDNSServers() []keenetic.DNSServerInfo {
	var domain *string
	if d.Domain != "" {
		domain = &d.Domain
	}
	return []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypeDoH,
			Proxy:    d.url,
			Endpoint: d.url,
			Domain:   domain,
		},
	}
}
