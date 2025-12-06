package upstreams

import (
	"bytes"
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
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
)

var (
	sharedDoHClient     *http.Client
	sharedDoHClientOnce sync.Once
)

// getSharedDoHClient returns the shared HTTP client for all DoH upstreams.
// This reduces memory overhead by sharing connection pools across upstreams.
func getSharedDoHClient() *http.Client {
	sharedDoHClientOnce.Do(func() {
		sharedDoHClient = &http.Client{
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
		}
	})
	return sharedDoHClient
}

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
		BaseUpstream: NewBaseUpstream(restrictedDomain),
		url:          urlStr,
		client:       getSharedDoHClient(), // Use shared client to reduce memory
	}
}

// Query sends a DNS query to the DoH upstream.
func (d *DoHUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	// Extract query info for logging
	queryInfo := "unknown"
	if len(req.Question) > 0 {
		q := req.Question[0]
		queryInfo = fmt.Sprintf("%s %s", q.Name, dns.TypeToString[q.Qtype])
	}

	upstreamStr := d.GetDNSStrings()[0]
	log.Debugf("[%04x] Querying upstream: %s for %s", req.Id, upstreamStr, queryInfo)

	packed, err := req.Pack()
	if err != nil {
		log.Debugf("[%04x] Upstream error for query %s (upstream: %s): failed to pack DNS message: %v", req.Id, queryInfo, upstreamStr, err)
		return nil, err
	}

	// Use bytes.NewReader instead of strings.NewReader to avoid []byte -> string conversion
	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, d.url, bytes.NewReader(packed))
	if err != nil {
		log.Debugf("[%04x] Upstream error for query %s (upstream: %s): failed to create HTTP request: %v", req.Id, queryInfo, upstreamStr, err)
		return nil, err
	}

	httpReq.Header.Set("Content-Type", dnsMessageContentType)
	httpReq.Header.Set("Accept", dnsMessageContentType)

	resp, err := d.client.Do(httpReq)
	if err != nil {
		// Check if it's a context timeout vs network timeout
		if ctx.Err() == context.DeadlineExceeded {
			log.Warnf("[%04x] Upstream timeout (context) for query: %s (upstream: %s)", req.Id, queryInfo, upstreamStr)
		} else {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				log.Debugf("[%04x] Upstream timeout (network) for query: %s (upstream: %s)", req.Id, queryInfo, upstreamStr)
			} else {
				log.Debugf("[%04x] Upstream error for query %s (upstream: %s): %v", req.Id, queryInfo, upstreamStr, err)
			}
		}
		return nil, err
	}
	defer utils.CloseOrWarn(resp.Body)

	if resp.StatusCode != http.StatusOK {
		log.Debugf("[%04x] Upstream error for query %s (upstream: %s): HTTP status %d", req.Id, queryInfo, upstreamStr, resp.StatusCode)
		return nil, err
	}

	// Read response body efficiently with single allocation
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		log.Debugf("[%04x] Upstream error for query %s (upstream: %s): failed to read response: %v", req.Id, queryInfo, upstreamStr, err)
		return nil, err
	}

	dnsResp := new(dns.Msg)
	if err := dnsResp.Unpack(body); err != nil {
		log.Debugf("[%04x] Upstream error for query %s (upstream: %s): failed to unpack response: %v", req.Id, queryInfo, upstreamStr, err)
		return nil, err
	}

	return dnsResp, nil
}

// Close closes any resources held by the upstream.
// Since we use a shared client, this is a no-op per upstream.
func (d *DoHUpstream) Close() error {
	// Shared client - connections managed globally, not per upstream
	return nil
}

// GetDNSStrings returns an array of DNS server strings in URL format.
func (d *DoHUpstream) GetDNSStrings() []string {
	displayURL := strings.TrimPrefix(d.url, httpsScheme)
	url := fmt.Sprintf("doh://%s", displayURL)
	if d.Domain != "" {
		url = fmt.Sprintf("%s?domain=%s", url, d.Domain)
	}
	return []string{url}
}
