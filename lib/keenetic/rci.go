package keenetic

import (
	"encoding/json"
	"fmt"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/utils"
	"io"
	"net/http"
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

// Generic function to fetch and deserialize JSON
func fetchAndDeserialize[T any](endpoint string) (T, error) {
	var result T

	// Make the HTTP request
	resp, err := http.Get(rciPrefix + endpoint)
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

func RciShowInterfaceMappedByIPNet() (map[string]Interface, error) {
	log.Debugf("Fetching interfaces from Keenetic...")
	if interfaces, err := RciShowInterfaceMappedById(); err != nil {
		return nil, err
	} else {
		mapped := make(map[string]Interface)
		for _, iface := range interfaces {
			if iface.Address != "" { // Check if IPv4 present
				if netmask, err := utils.IPv4ToNetmask(iface.Address, iface.Mask); err == nil {
					mapped[netmask.String()] = iface
				}
			}

			if iface.IPv6.Addresses != nil { // Check if IPv6 present
				for _, addr := range iface.IPv6.Addresses {
					if netmask, err := utils.IPv6ToNetmask(addr.Address, addr.PrefixLength); err == nil {
						mapped[netmask.String()] = iface
					}
				}
			}
		}
		return mapped, nil
	}
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
