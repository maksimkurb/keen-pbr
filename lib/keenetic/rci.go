package keenetic

import (
	"encoding/json"
	"fmt"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/utils"
	"io"
	"net/http"
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

// RciShowInterfaceMappedById returns a map of interfaces in Keenetic
func RciShowInterfaceMappedById() (map[string]Interface, error) {
	return fetchAndDeserialize[map[string]Interface]("/show/interface")
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
