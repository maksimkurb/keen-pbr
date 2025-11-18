package keenetic

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
)

// Client is the main client for interacting with the Keenetic Router RCI API.
//
// The client provides methods for retrieving router information including
// version, interfaces, and DNS configuration. All methods are safe for
// concurrent use.
type Client struct {
	httpClient HTTPClient
	baseURL    string
	cache      *Cache
}

// NewClient creates a new Keenetic RCI API client with the default base URL.
//
// The default base URL is http://localhost:79/rci (local RCI endpoint).
// If httpClient is nil, a default HTTP client will be used.
//
// For custom base URLs, use NewClientWithBaseURL.
func NewClient(httpClient HTTPClient) *Client {
	if httpClient == nil {
		httpClient = &defaultHTTPClient{}
	}

	return &Client{
		httpClient: httpClient,
		baseURL:    rciPrefix,
		cache:      NewCache(0), // No TTL - cache forever
	}
}

// NewClientWithBaseURL creates a new Keenetic RCI API client with a custom base URL.
//
// This is useful for:
//   - Connecting to remote Keenetic routers (e.g., "http://192.168.1.1/rci")
//   - Testing with mock servers
//   - Custom RCI proxy endpoints
//
// Example:
//
//	client := keenetic.NewClientWithBaseURL("http://192.168.1.1/rci", nil)
func NewClientWithBaseURL(baseURL string, httpClient HTTPClient) *Client {
	if httpClient == nil {
		httpClient = &defaultHTTPClient{}
	}

	return &Client{
		httpClient: httpClient,
		baseURL:    baseURL,
		cache:      NewCache(0), // No TTL - cache forever
	}
}

// fetchAndDeserialize is a generic helper function to fetch and deserialize JSON from the API.
func fetchAndDeserializeForClient[T any](c *Client, endpoint string) (T, error) {
	var result T

	resp, err := c.httpClient.Get(c.baseURL + endpoint)
	if err != nil {
		return result, fmt.Errorf("failed to fetch %s: %w", endpoint, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return result, fmt.Errorf("unexpected status code %d for %s", resp.StatusCode, endpoint)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return result, fmt.Errorf("failed to read response body: %w", err)
	}

	if err := json.Unmarshal(body, &result); err != nil {
		return result, fmt.Errorf("failed to unmarshal response: %w", err)
	}

	return result, nil
}

// GetVersion retrieves the Keenetic OS version.
//
// The version is cached after the first successful retrieval to avoid
// repeated API calls.
func (c *Client) GetVersion() (*KeeneticVersion, error) {
	// Check cache first
	if version, found := c.cache.GetVersion(); found {
		return version, nil
	}

	// Fetch version from API
	versionStr, err := fetchAndDeserializeForClient[string](c, "/show/version/release")
	if err != nil {
		return nil, fmt.Errorf("failed to get Keenetic version: %w", err)
	}

	version, err := parseVersion(versionStr)
	if err != nil {
		return nil, err
	}

	// Cache the version
	c.cache.SetVersion(version)
	return version, nil
}

// GetRawVersion retrieves the raw Keenetic OS version string from the router.
//
// Unlike GetVersion, this returns the full version string as-is (e.g., "5.00.B.3.0-2")
// without parsing it into major/minor components.
func (c *Client) GetRawVersion() (string, error) {
	versionStr, err := fetchAndDeserializeForClient[string](c, "/show/version/release")
	if err != nil {
		return "", fmt.Errorf("failed to get Keenetic version: %w", err)
	}
	return versionStr, nil
}

// GetInterfaces retrieves all network interfaces from the Keenetic router,
// mapped by their system names (Linux interface names).
//
// This method supports both modern (4.03+) and legacy Keenetic OS versions,
// automatically detecting the version and using the appropriate API.
func (c *Client) GetInterfaces() (map[string]Interface, error) {
	// Fetch all interfaces
	interfaces, err := fetchAndDeserializeForClient[Interfaces](c, "/show/interface")
	if err != nil {
		return nil, err
	}

	// Convert to map
	interfaceMap := make(map[string]Interface)
	for _, iface := range interfaces {
		interfaceMap[iface.ID] = iface
	}

	// Get version to determine how to populate SystemName
	version, err := c.GetVersion()
	if err != nil {
		return nil, err
	}

	// Populate SystemName field
	if supportsSystemNameEndpoint(version) {
		return c.populateSystemNamesModern(interfaceMap)
	}
	return c.populateSystemNamesLegacy(interfaceMap)
}

// GetDNSServers retrieves the list of DNS servers configured on the router.
func (c *Client) GetDNSServers() ([]DnsServerInfo, error) {
	dnsProxyConfig, err := fetchAndDeserializeForClient[string](c, "/show/dns/proxy")
	if err != nil {
		return nil, err
	}

	return ParseDNSProxyConfig(dnsProxyConfig), nil
}


// ClearCache clears all cached data.
//
// This is useful for testing or when you need to force a refresh of cached data.
func (c *Client) ClearCache() {
	c.cache.Clear()
}
