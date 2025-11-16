package keenetic

import (
	"fmt"
	"net/url"
	"strconv"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/vishvananda/netlink"
)

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

// supportsSystemNameEndpoint returns true if Keenetic OS version supports /show/interface/system-name endpoint (4.03+)
func supportsSystemNameEndpoint(version *KeeneticVersion) bool {
	if version == nil {
		return false
	}
	return version.Major > 4 || (version.Major == 4 && version.Minor >= 3)
}

// getSystemNameForInterface fetches the Linux system name for a Keenetic interface using the RCI API
func (c *Client) getSystemNameForInterface(interfaceID string) (string, error) {
	endpoint := "/show/interface/system-name?name=" + url.QueryEscape(interfaceID)
	systemName, err := fetchAndDeserializeForClient[string](c, endpoint)
	if err != nil {
		return "", err
	}
	return systemName, nil
}

// populateSystemNamesModern populates SystemName field using the system-name endpoint (Keenetic 4.03+)
func (c *Client) populateSystemNamesModern(interfaces map[string]Interface) (map[string]Interface, error) {
	result := make(map[string]Interface)
	for id, iface := range interfaces {
		systemName, err := c.getSystemNameForInterface(id)
		if err != nil {
			log.Debugf("Failed to get system name for interface %s: %v", id, err)
			continue
		}
		iface.SystemName = systemName
		result[systemName] = iface
	}
	return result, nil
}

// populateSystemNamesLegacy populates SystemName field by matching IP addresses (for older Keenetic versions)
func (c *Client) populateSystemNamesLegacy(interfaces map[string]Interface) (map[string]Interface, error) {
	systemInterfaces, err := getSystemInterfacesHelper()
	if err != nil {
		log.Warnf("Failed to get system interfaces: %v", err)
		return make(map[string]Interface), err
	}

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

	return result, nil
}

// SystemInterface represents a Linux network interface with its IP addresses
type SystemInterface struct {
	Name string
	IPs  []string
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

// parseDNSProxyConfig parses the proxy config string and returns DNS server info
func parseDNSProxyConfig(config string) []DnsServerInfo {
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
