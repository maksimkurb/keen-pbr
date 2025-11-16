package keenetic

import (
	"net/url"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/vishvananda/netlink"
)

// SystemInterface represents a Linux network interface with its IP addresses.
//
// This is used for legacy interface mapping where Keenetic interfaces are
// matched to system interfaces by comparing IP addresses.
type SystemInterface struct {
	Name string
	IPs  []string
}

// getSystemNameForInterface fetches the Linux system name for a Keenetic interface
// using the RCI API /show/interface/system-name endpoint.
//
// This method is only available on Keenetic OS 4.03+. For older versions,
// use legacy IP address matching instead.
func (c *Client) getSystemNameForInterface(interfaceID string) (string, error) {
	endpoint := "/show/interface/system-name?name=" + url.QueryEscape(interfaceID)
	systemName, err := fetchAndDeserializeForClient[string](c, endpoint)
	if err != nil {
		return "", err
	}
	return systemName, nil
}

// populateSystemNamesModern populates SystemName field using the system-name
// endpoint (Keenetic OS 4.03+).
//
// This is the preferred method for modern Keenetic OS versions as it directly
// queries the system name without needing IP address matching.
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

// populateSystemNamesLegacy populates SystemName field by matching IP addresses
// (for older Keenetic versions before 4.03).
//
// This method retrieves all system network interfaces and matches them with
// Keenetic interfaces by comparing IPv4 and IPv6 addresses.
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

// getSystemInterfacesHelper gets all system network interfaces with their IP addresses.
//
// This function uses netlink to retrieve all network interfaces and their
// associated IPv4 and IPv6 addresses.
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
