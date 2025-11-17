package keenetic

import (
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ParseDNSProxyConfig parses the Keenetic DNS proxy configuration string and
// returns a list of DNS server information.
//
// The configuration format includes lines like:
//   dns_server = 192.168.1.1 domain.com
//   dns_server = 127.0.0.1:40500 . # p0.freedns.controld.com (DoT)
//   dns_server = 127.0.0.1:40501 . # https://dns.example.com (DoH)
//
// This function extracts DNS server type (Plain IPv4, Plain IPv6, DoT, DoH),
// the proxy address, endpoint, port, and optional domain.
func ParseDNSProxyConfig(config string) []DnsServerInfo {
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

		server := parseDNSServerEntry(addr, comment)
		server.Domain = domain
		servers = append(servers, server)
	}

	return servers
}

// parseDNSServerEntry parses a single DNS server entry from the address and comment.
//
// It determines the server type (Plain, DoT, DoH) based on the address format
// and the presence of specific patterns in the comment.
func parseDNSServerEntry(addr, comment string) DnsServerInfo {
	var endpoint string
	var typ DnsServerType
	var port string
	var ipOnly string

	if strings.HasPrefix(addr, localhostPrefix) {
		// Local proxy, check for DoT/DoH in comment
		if comment != "" && strings.HasPrefix(comment, httpsPrefix) {
			// DoH (DNS over HTTPS)
			typ = DnsServerTypeDoH
			endpoint = extractDoHURI(comment)
			if endpoint == "" {
				log.Errorf("Malformed DoH URI in comment: %q", comment)
				// Fallback to plain
				typ = DnsServerTypePlain
				endpoint = addr
			}
			ipOnly, port = splitAddressPort(addr)
		} else if comment != "" {
			// DoT (DNS over TLS) - treat any comment as DoT SNI
			typ = DnsServerTypeDoT
			endpoint = comment
			ipOnly, port = splitAddressPort(addr)
		} else {
			// Plain DNS via localhost
			typ = DnsServerTypePlain
			endpoint = addr
			port = ""
			ipOnly = addr
		}
	} else if strings.Contains(addr, ".") {
		// IPv4 (possibly with port)
		typ = DnsServerTypePlain
		ipOnly, port = splitIPv4AddressPort(addr)
		endpoint = ipOnly
	} else {
		// IPv6
		typ = DnsServerTypePlainIPv6
		ipOnly = addr
		endpoint = addr
		port = ""
	}

	return DnsServerInfo{
		Type:     typ,
		Proxy:    ipOnly,
		Endpoint: endpoint,
		Port:     port,
	}
}

// extractDoHURI extracts the DoH URI from a comment, removing any @ suffix.
//
// Example: "https://freedns.controld.com/p0@dnsm" -> "https://freedns.controld.com/p0"
func extractDoHURI(comment string) string {
	uri := comment
	if idx := strings.Index(uri, atSymbol); idx != -1 {
		uri = uri[:idx]
	}
	return uri
}

// splitAddressPort splits an address:port string into address and port components.
//
// Example: "127.0.0.1:40500" -> ("127.0.0.1", "40500")
func splitAddressPort(addr string) (ip, port string) {
	if idx := strings.LastIndex(addr, ":"); idx != -1 && idx < len(addr)-1 {
		return addr[:idx], addr[idx+1:]
	}
	return addr, ""
}

// splitIPv4AddressPort splits an IPv4 address:port string.
//
// Similar to splitAddressPort but with additional validation for IPv4.
func splitIPv4AddressPort(addr string) (ip, port string) {
	if idx := strings.LastIndex(addr, ":"); idx != -1 && idx > 0 && idx < len(addr)-1 {
		return addr[:idx], addr[idx+1:]
	}
	return addr, ""
}
