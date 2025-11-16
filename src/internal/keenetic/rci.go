package keenetic

import (
	"net/http"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

const (
	dnsServerPrefix  = "dns_server = "
	localhostPrefix  = "127.0.0.1:"
	httpsPrefix      = "https://"
	atSymbol         = "@"
	dotSymbol        = "."
	commentDelimiter = "#"
)

// HTTPClient interface for dependency injection in tests
type HTTPClient interface {
	Get(url string) (*http.Response, error)
}

// defaultHTTPClient implements HTTPClient using the standard http package
type defaultHTTPClient struct{}

func (c *defaultHTTPClient) Get(url string) (*http.Response, error) {
	return http.Get(url)
}

// KeeneticVersion represents the version of Keenetic OS
type KeeneticVersion struct {
	Major int
	Minor int
}

// fetchAndDeserialize is a generic function to fetch and deserialize JSON (deprecated, use Client method)
//
// Deprecated: This function uses global state. Use fetchAndDeserializeForClient instead.
func fetchAndDeserialize[T any](endpoint string) (T, error) {
	return fetchAndDeserializeForClient[T](defaultClient, endpoint)
}

// fetchAndDeserializeWithRetry calls fetchAndDeserialize with up to 3 attempts and 3s interval between them on failure
func fetchAndDeserializeWithRetry[T any](endpoint string) (T, error) {
	var lastErr error
	var zero T
	for attempt := 1; attempt <= 3; attempt++ {
		result, err := fetchAndDeserialize[T](endpoint)
		if err == nil {
			return result, nil
		}
		lastErr = err
		if attempt < 3 {
			log.Warnf("Failed to make RCI call %s (%s), retrying in 3s...", endpoint, err.Error())
			time.Sleep(3 * time.Second)
		}
	}
	return zero, lastErr
}

// RciShowInterfaceMappedById returns a map of interfaces in Keenetic
//
// Deprecated: Use Client.GetInterfaces() for better testability.
func RciShowInterfaceMappedById() (map[string]Interface, error) {
	return fetchAndDeserializeWithRetry[map[string]Interface]("/show/interface")
}

// RciShowInterfaceMappedByIPNet is deprecated, use RciShowInterfaceMappedBySystemName instead
//
// Deprecated: Use Client.GetInterfaces() for better testability.
func RciShowInterfaceMappedByIPNet() (map[string]Interface, error) {
	return defaultClient.GetInterfaces()
}

// ParseDnsProxyConfig parses the proxy config string and returns DNS server info
//
// Deprecated: Use parseDNSProxyConfig from helpers.go instead.
func ParseDnsProxyConfig(config string) []DnsServerInfo {
	return parseDNSProxyConfig(config)
}

// RciShowDnsServers fetches all DNS servers from Keenetic RCI and returns them for the System policy only
//
// Deprecated: Use Client.GetDNSServers() for better testability.
func RciShowDnsServers() ([]DnsServerInfo, error) {
	type proxyStatusEntry struct {
		ProxyName   string `json:"proxy-name"`
		ProxyConfig string `json:"proxy-config"`
	}
	type dnsProxyResponse struct {
		ProxyStatus []proxyStatusEntry `json:"proxy-status"`
	}

	resp, err := fetchAndDeserializeWithRetry[dnsProxyResponse]("/show/dns-proxy")
	if err != nil {
		return nil, err
	}

	for _, entry := range resp.ProxyStatus {
		if entry.ProxyName != "System" {
			continue
		}
		return parseDNSProxyConfig(entry.ProxyConfig), nil // Only return the System policy's servers
	}
	return nil, nil // No System policy found
}
