package keenetic

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"

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

// bulkSystemNameRequest represents the bulk POST request to fetch system names
type bulkSystemNameRequest struct {
	Show bulkSystemNameShow `json:"show"`
}

type bulkSystemNameShow struct {
	Interface []bulkSystemNameInterface `json:"interface"`
}

type bulkSystemNameInterface struct {
	SystemName bulkSystemNameQuery `json:"system-name"`
}

type bulkSystemNameQuery struct {
	Name string `json:"name"`
}

// bulkSystemNameResponse represents the bulk response from Keenetic
type bulkSystemNameResponse struct {
	Show bulkSystemNameResponseShow `json:"show"`
}

type bulkSystemNameResponseShow struct {
	Interface []bulkSystemNameResult `json:"interface"`
}

// bulkSystemNameResult can be either a successful system name string or an error object
type bulkSystemNameResult struct {
	SystemName interface{} `json:"system-name"` // Can be string or systemNameError
}



// getSystemNamesForInterfacesBulk fetches system names for multiple interfaces in a single request.
//
// This uses a bulk POST request to /rci to query all interfaces at once,
// significantly reducing API calls and improving performance.
//
// Returns a map of Keenetic interface ID -> system name (Linux interface name).
// Interfaces that don't exist or have errors are logged and skipped.
func (c *Client) getSystemNamesForInterfacesBulk(interfaceIDs []string) (map[string]string, error) {
	if len(interfaceIDs) == 0 {
		return make(map[string]string), nil
	}

	// Build the bulk request
	request := bulkSystemNameRequest{
		Show: bulkSystemNameShow{
			Interface: make([]bulkSystemNameInterface, len(interfaceIDs)),
		},
	}

	for i, id := range interfaceIDs {
		request.Show.Interface[i] = bulkSystemNameInterface{
			SystemName: bulkSystemNameQuery{Name: id},
		}
	}

	// Marshal request to JSON
	requestBody, err := json.Marshal(request)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal bulk request: %w", err)
	}

	// Make POST request
	resp, err := c.httpClient.Post(c.baseURL+"/", "application/json", requestBody)
	if err != nil {
		return nil, fmt.Errorf("failed to make bulk request: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d for bulk request", resp.StatusCode)
	}

	// Read and parse response
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response body: %w", err)
	}

	var response bulkSystemNameResponse
	if err := json.Unmarshal(body, &response); err != nil {
		return nil, fmt.Errorf("failed to unmarshal response: %w", err)
	}

	// Process results
	result := make(map[string]string)
	for i, ifaceResult := range response.Show.Interface {
		if i >= len(interfaceIDs) {
			log.Warnf("Bulk response has more interfaces than requested, ignoring extras")
			break
		}

		interfaceID := interfaceIDs[i]

		// Check if system-name is a string or an error object
		switch v := ifaceResult.SystemName.(type) {
		case string:
			// Success - we got the system name
			result[interfaceID] = v
		case map[string]interface{}:
			// Error object - check if it has status field
			if statusArray, ok := v["status"].([]interface{}); ok && len(statusArray) > 0 {
				if statusObj, ok := statusArray[0].(map[string]interface{}); ok {
					message := statusObj["message"]
					log.Debugf("Failed to get system name for interface %s: %v", interfaceID, message)
				}
			} else {
				log.Debugf("Failed to get system name for interface %s: unexpected error format", interfaceID)
			}
		default:
			log.Debugf("Failed to get system name for interface %s: unexpected response type %T", interfaceID, v)
		}
	}

	return result, nil
}

// populateSystemNamesModern populates SystemName field using the system-name
// endpoint (Keenetic OS 4.03+).
//
// This is the preferred method for modern Keenetic OS versions as it directly
// queries the system name without needing IP address matching.
//
// Uses a bulk POST request to fetch all system names in a single API call,
// improving performance significantly when multiple interfaces are present.
//
// When multiple Keenetic interfaces map to the same Linux system name
// (e.g., GigabitEthernet1 and GigabitEthernet1/0 both map to eth3),
// prefers the parent interface (without slash suffix) which typically has
// complete status information (connected, description fields).
func (c *Client) populateSystemNamesModern(interfaces map[string]Interface) (map[string]Interface, error) {
	// Collect all interface IDs
	interfaceIDs := make([]string, 0, len(interfaces))
	for id := range interfaces {
		interfaceIDs = append(interfaceIDs, id)
	}

	// Fetch all system names in bulk
	systemNames, err := c.getSystemNamesForInterfacesBulk(interfaceIDs)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch system names in bulk: %w", err)
	}

	// Map interfaces by system name, with preference for parent interfaces
	result := make(map[string]Interface)
	for id, systemName := range systemNames {
		iface := interfaces[id]
		iface.SystemName = systemName

		// Check if we already have an interface for this system name
		if existing, exists := result[systemName]; exists {
			// Prefer the interface that is more likely to be the parent:
			// 1. Prefer interface without slash in ID (e.g., "GigabitEthernet1" over "GigabitEthernet1/0")
			// 2. Prefer interface with non-empty Connected field
			// 3. Prefer interface with non-empty Description field

			existingIsParent := !hasSlashInID(existing.ID)
			newIsParent := !hasSlashInID(iface.ID)

			existingHasConnected := existing.Connected != ""
			newHasConnected := iface.Connected != ""

			existingHasDescription := existing.Description != ""
			newHasDescription := iface.Description != ""

			// Prefer parent over child
			if newIsParent && !existingIsParent {
				result[systemName] = iface
				log.Debugf("Preferring parent interface %s over child %s for system name %s",
					iface.ID, existing.ID, systemName)
				continue
			} else if existingIsParent && !newIsParent {
				// Keep existing parent
				continue
			}

			// Both are parents or both are children - prefer one with more data
			if newHasConnected && !existingHasConnected {
				result[systemName] = iface
				log.Debugf("Preferring interface %s (has connected) over %s for system name %s",
					iface.ID, existing.ID, systemName)
			} else if newHasDescription && !existingHasDescription {
				result[systemName] = iface
				log.Debugf("Preferring interface %s (has description) over %s for system name %s",
					iface.ID, existing.ID, systemName)
			}
			// Otherwise keep existing
		} else {
			// First interface for this system name
			result[systemName] = iface
		}
	}

	return result, nil
}

// hasSlashInID checks if the interface ID contains a slash, indicating
// it's a child interface (e.g., GigabitEthernet1/0, WifiMaster0/AccessPoint0)
func hasSlashInID(id string) bool {
	for i := 0; i < len(id); i++ {
		if id[i] == '/' {
			return true
		}
	}
	return false
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
