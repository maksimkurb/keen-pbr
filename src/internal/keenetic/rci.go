package keenetic

import (
	"encoding/json"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/vishvananda/netlink"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

const (
	dnsServerPrefix   = "dns_server = "
	localhostPrefix   = "127.0.0.1:"
	httpsPrefix       = "https://"
	atSymbol          = "@"
	dotSymbol         = "."
	commentDelimiter  = "#"
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

// Global HTTP client (can be overridden in tests)
var httpClient HTTPClient = &defaultHTTPClient{}

// Cached Keenetic version info
var keeneticVersionCache *KeeneticVersion = nil

type KeeneticVersion struct {
	Major int
	Minor int
}

// parseVersion parses version string like "4.03.C.6.3-9" and returns major/minor
func parseVersion(versionStr string) (*KeeneticVersion, error) {
	parts := strings.Split(versionStr, ".")
	if len(parts) < 2 {
		return nil, fmt.Errorf("invalid version format: %s", versionStr)
	}

	major, err := strconv.Atoi(parts[0])
	if err != nil {
		return nil, fmt.Errorf("invalid major version: %s", parts[0])
	}

	minor, err := strconv.Atoi(parts[1])
	if err != nil {
		return nil, fmt.Errorf("invalid minor version: %s", parts[1])
	}

	return &KeeneticVersion{Major: major, Minor: minor}, nil
}

// GetKeeneticVersion fetches and caches the Keenetic OS version
func GetKeeneticVersion() (*KeeneticVersion, error) {
	if keeneticVersionCache != nil {
		return keeneticVersionCache, nil
	}

	versionStr, err := fetchAndDeserialize[string]("/show/version/release")
	if err != nil {
		return nil, fmt.Errorf("failed to get Keenetic version: %w", err)
	}

	version, err := parseVersion(versionStr)
	if err != nil {
		return nil, err
	}

	keeneticVersionCache = version
	log.Debugf("Detected Keenetic OS version: %d.%02d", version.Major, version.Minor)
	return version, nil
}

// supportsSystemNameEndpoint returns true if Keenetic OS version supports /show/interface/system-name endpoint (4.03+)
func supportsSystemNameEndpoint(version *KeeneticVersion) bool {
	if version == nil {
		return false
	}
	return version.Major > 4 || (version.Major == 4 && version.Minor >= 3)
}

// Generic function to fetch and deserialize JSON
func fetchAndDeserialize[T any](endpoint string) (T, error) {
	var result T

	// Make the HTTP request
	resp, err := httpClient.Get(rciPrefix + endpoint)
	if err != nil {
		return result, fmt.Errorf("failed to fetch data: %w", err)
	}
	defer resp.Body.Close()

	// Check if the HTTP status code is OK
	if resp.StatusCode != http.StatusOK {
		return result, fmt.Errorf("received non-OK HTTP status: %s", resp.Status)
	}

	// Read the response body
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return result, fmt.Errorf("failed to read response body: %w", err)
	}

	// Parse the JSON response
	err = json.Unmarshal(body, &result)
	if err != nil {
		return result, fmt.Errorf("failed to parse JSON: %w", err)
	}

	return result, nil
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
func RciShowInterfaceMappedById() (map[string]Interface, error) {
	return fetchAndDeserializeWithRetry[map[string]Interface]("/show/interface")
}

// getSystemNameForInterface fetches the Linux system name for a Keenetic interface using the RCI API
func getSystemNameForInterface(interfaceID string) (string, error) {
	endpoint := "/show/interface/system-name?name=" + url.QueryEscape(interfaceID)
	systemName, err := fetchAndDeserialize[string](endpoint)
	if err != nil {
		return "", err
	}
	return systemName, nil
}

// populateSystemNamesModern populates SystemName field using the system-name endpoint (Keenetic 4.03+)
func populateSystemNamesModern(interfaces map[string]Interface) map[string]Interface {
	result := make(map[string]Interface)
	for id, iface := range interfaces {
		systemName, err := getSystemNameForInterface(id)
		if err != nil {
			log.Debugf("Failed to get system name for interface %s: %v", id, err)
			continue
		}
		iface.SystemName = systemName
		result[systemName] = iface
	}
	return result
}

// populateSystemNamesLegacy populates SystemName field by matching IP addresses (for older Keenetic versions)
func populateSystemNamesLegacy(interfaces map[string]Interface, systemInterfaces []SystemInterface) map[string]Interface {
	result := make(map[string]Interface)

	// Create maps of Keenetic interface IPs for quick lookup
	keeneticIPv4 := make(map[string]string) // IP -> interface ID
	keeneticIPv6 := make(map[string]string) // IP -> interface ID

	for id, iface := range interfaces {
		if iface.Address != "" {
			if netmask, err := utils.IPv4ToNetmask(iface.Address, iface.Mask); err == nil {
				keeneticIPv4[netmask.String()] = id
			}
		}
		if iface.IPv6.Addresses != nil {
			for _, addr := range iface.IPv6.Addresses {
				if netmask, err := utils.IPv6ToNetmask(addr.Address, addr.PrefixLength); err == nil {
					keeneticIPv6[netmask.String()] = id
				}
			}
		}
	}

	// Match system interfaces with Keenetic interfaces
	for _, sysIface := range systemInterfaces {
		var matchedID string

		// Try to match by IP addresses
		for _, ip := range sysIface.IPs {
			if id, ok := keeneticIPv4[ip]; ok {
				matchedID = id
				break
			}
			if id, ok := keeneticIPv6[ip]; ok {
				matchedID = id
				break
			}
		}

		if matchedID != "" {
			iface := interfaces[matchedID]
			iface.SystemName = sysIface.Name
			result[sysIface.Name] = iface
		}
	}

	return result
}

// SystemInterface represents a Linux network interface with its IP addresses
type SystemInterface struct {
	Name string
	IPs  []string
}

func RciShowInterfaceMappedBySystemName() (map[string]Interface, error) {
	log.Debugf("Fetching interfaces from Keenetic...")
	interfaces, err := RciShowInterfaceMappedById()
	if err != nil {
		return nil, err
	}

	// Try to get Keenetic version and use modern approach if supported
	version, err := GetKeeneticVersion()
	if err != nil {
		log.Warnf("Failed to detect Keenetic version, will use legacy IP matching: %v", err)
		// Fall back to legacy approach
		return populateSystemNamesLegacyFromNetlink(interfaces), nil
	}

	if supportsSystemNameEndpoint(version) {
		log.Debugf("Using system-name endpoint (Keenetic %d.%02d+)", version.Major, version.Minor)
		return populateSystemNamesModern(interfaces), nil
	}

	log.Debugf("Using legacy IP matching (Keenetic %d.%02d)", version.Major, version.Minor)
	return populateSystemNamesLegacyFromNetlink(interfaces), nil
}

// populateSystemNamesLegacyFromNetlink gets system interfaces using netlink and matches with Keenetic interfaces
func populateSystemNamesLegacyFromNetlink(interfaces map[string]Interface) map[string]Interface {
	// Import netlink dynamically to avoid circular dependency
	// We'll call a function from networking package
	return populateSystemNamesLegacyWithHelper(interfaces, getSystemInterfacesHelper)
}

// Helper function type for getting system interfaces
type SystemInterfaceGetter func() ([]SystemInterface, error)

// populateSystemNamesLegacyWithHelper allows dependency injection for testing
func populateSystemNamesLegacyWithHelper(interfaces map[string]Interface, getter SystemInterfaceGetter) map[string]Interface {
	systemInterfaces, err := getter()
	if err != nil {
		log.Warnf("Failed to get system interfaces: %v", err)
		return make(map[string]Interface)
	}
	return populateSystemNamesLegacy(interfaces, systemInterfaces)
}

// getSystemInterfacesHelper gets all system interfaces with their IP addresses
func getSystemInterfacesHelper() ([]SystemInterface, error) {
	links, err := netlink.LinkList()
	if err != nil {
		return nil, err
	}

	var result []SystemInterface
	for _, link := range links {
		addrs, err := netlink.AddrList(link, netlink.FAMILY_ALL)
		if err != nil {
			log.Debugf("Failed to get addresses for interface %s: %v", link.Attrs().Name, err)
			continue
		}

		var ips []string
		for _, addr := range addrs {
			ips = append(ips, addr.IPNet.String())
		}

		result = append(result, SystemInterface{
			Name: link.Attrs().Name,
			IPs:  ips,
		})
	}

	return result, nil
}

// RciShowInterfaceMappedByIPNet is deprecated, use RciShowInterfaceMappedBySystemName instead
// This function is kept for backward compatibility and returns the same data with SystemName populated
func RciShowInterfaceMappedByIPNet() (map[string]Interface, error) {
	return RciShowInterfaceMappedBySystemName()
}

// ParseDnsProxyConfig parses the proxy config string and returns DNS server info
func ParseDnsProxyConfig(config string) []DnsServerInfo {
	var servers []DnsServerInfo
	lines := strings.Split(config, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, dnsServerPrefix) {
			continue
		}
		// Remove prefix
		val := strings.TrimPrefix(line, dnsServerPrefix)
		// Remove comments
		var comment string
		if idx := strings.Index(val, commentDelimiter); idx != -1 {
			comment = strings.TrimSpace(val[idx+1:])
			val = val[:idx]
		}
		val = strings.TrimSpace(val)
		// Split by whitespace
		parts := strings.Fields(val)
		if len(parts) == 0 {
			log.Errorf("Empty or malformed dns_server line: %q", line)
			continue
		}
		addr := parts[0]
		var domain *string
		if len(parts) > 1 && parts[1] != dotSymbol {
			domain = &parts[1]
		}

		var endpoint string
		var typ DnsServerType
		var port string
		var ipOnly string

		if strings.HasPrefix(addr, localhostPrefix) {
			// Local proxy, check for DoT/DoH in comment
			if comment != "" && strings.HasPrefix(comment, httpsPrefix) {
				typ = DnsServerTypeDoH
				// Extract URI from comment (e.g. https://freedns.controld.com/p0@dnsm)
				uri := comment
				if idx := strings.Index(uri, atSymbol); idx != -1 {
					uri = uri[:idx]
				}
				if uri == "" {
					log.Errorf("Malformed DoH URI in line: %q", line)
					continue
				}
				endpoint = uri
				// Extract port from addr
				if idx := strings.LastIndex(addr, ":"); idx != -1 && idx < len(addr)-1 {
					port = addr[idx+1:]
					ipOnly = addr[:idx]
				} else {
					port = ""
					ipOnly = addr
				}
			} else if comment != "" { // treat any comment as DoT SNI
				typ = DnsServerTypeDoT
				endpoint = comment
				// Extract port from addr
				if idx := strings.LastIndex(addr, ":"); idx != -1 && idx < len(addr)-1 {
					port = addr[idx+1:]
					ipOnly = addr[:idx]
				} else {
					port = ""
					ipOnly = addr
				}
			} else {
				typ = DnsServerTypePlain
				endpoint = addr
				port = ""
				ipOnly = addr
			}
		} else if strings.Contains(addr, ".") {
			// IPv4 (possibly with port)
			typ = DnsServerTypePlain
			if idx := strings.LastIndex(addr, ":"); idx != -1 && idx > 0 && idx < len(addr)-1 {
				ipOnly = addr[:idx]
				port = addr[idx+1:]
			} else {
				ipOnly = addr
				port = ""
			}
			endpoint = ipOnly
		} else {
			// IPv6
			typ = DnsServerTypePlainIPv6
			ipOnly = addr
			endpoint = addr
			port = ""
		}

		servers = append(servers, DnsServerInfo{
			Type:     typ,
			Domain:   domain,
			Proxy:    ipOnly,
			Endpoint: endpoint,
			Port:     port,
		})
	}
	return servers
}

// RciShowDnsServers fetches all DNS servers from Keenetic RCI and returns them for the System policy only
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
		return ParseDnsProxyConfig(entry.ProxyConfig), nil // Only return the System policy's servers
	}
	return nil, nil // No System policy found
}
