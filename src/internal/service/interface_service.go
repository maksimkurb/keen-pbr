package service

import (
	"fmt"
	"net"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/vishvananda/netlink"
)

// InterfaceInfo represents a network interface with optional Keenetic metadata.
type InterfaceInfo struct {
	Index       int      `json:"index"`
	Name        string   `json:"name"`
	IsUp        bool     `json:"is_up"`
	IsLoopback  bool     `json:"is_loopback"`
	IPAddresses []string `json:"ip_addresses,omitempty"`

	// Keenetic-specific fields (nil if Keenetic API not available)
	KeeneticID          string `json:"keenetic_id,omitempty"`
	KeeneticDescription string `json:"keenetic_description,omitempty"`
	KeeneticLink        string `json:"keenetic_link,omitempty"`
	KeeneticConnected   string `json:"keenetic_connected,omitempty"`
}

// InterfaceService provides unified interface information for both CLI and API.
type InterfaceService struct {
	keeneticClient domain.KeeneticClient
}

// NewInterfaceService creates a new interface service.
func NewInterfaceService(keeneticClient domain.KeeneticClient) *InterfaceService {
	return &InterfaceService{keeneticClient: keeneticClient}
}

// GetInterfaces returns all network interfaces with Keenetic metadata if available.
func (s *InterfaceService) GetInterfaces(includeIPs bool, includeLoopback bool) ([]InterfaceInfo, error) {
	// Get system interfaces
	links, err := netlink.LinkList()
	if err != nil {
		return nil, fmt.Errorf("failed to get network interfaces: %w", err)
	}

	// Get Keenetic interfaces if available
	var keeneticIfaces map[string]keenetic.Interface
	if s.keeneticClient != nil {
		keeneticIfaces, err = s.keeneticClient.GetInterfaces()
		if err != nil {
			log.Warnf("Failed to get Keenetic interfaces: %v", err)
		}
	}

	result := make([]InterfaceInfo, 0, len(links))
	for _, link := range links {
		attrs := link.Attrs()
		isLoopback := attrs.Flags&net.FlagLoopback != 0

		// Skip loopback if not requested
		if isLoopback && !includeLoopback {
			continue
		}

		info := InterfaceInfo{
			Index:      attrs.Index,
			Name:       attrs.Name,
			IsUp:       attrs.Flags&net.FlagUp != 0,
			IsLoopback: isLoopback,
		}

		// Add IP addresses if requested
		if includeIPs {
			addrs, err := netlink.AddrList(link, netlink.FAMILY_ALL)
			if err == nil {
				info.IPAddresses = make([]string, 0, len(addrs))
				for _, addr := range addrs {
					info.IPAddresses = append(info.IPAddresses, addr.IPNet.String())
				}
			}
		}

		// Add Keenetic metadata if available
		if keeneticIfaces != nil {
			if keeneticIface, ok := keeneticIfaces[attrs.Name]; ok {
				info.KeeneticID = keeneticIface.ID
				info.KeeneticDescription = keeneticIface.Description
				info.KeeneticLink = keeneticIface.Link
				info.KeeneticConnected = keeneticIface.Connected
			}
		}

		result = append(result, info)
	}

	return result, nil
}

// GetInterfacesFromList converts networking.Interface list to InterfaceInfo list.
// This is useful when interfaces are already loaded (e.g., from AppContext).
func (s *InterfaceService) GetInterfacesFromList(ifaces []networking.Interface, includeIPs bool) ([]InterfaceInfo, error) {
	// Get Keenetic interfaces if available
	var keeneticIfaces map[string]keenetic.Interface
	var err error
	if s.keeneticClient != nil {
		keeneticIfaces, err = s.keeneticClient.GetInterfaces()
		if err != nil {
			log.Warnf("Failed to get Keenetic interfaces: %v", err)
		}
	}

	result := make([]InterfaceInfo, 0, len(ifaces))
	for _, iface := range ifaces {
		attrs := iface.Attrs()

		info := InterfaceInfo{
			Index:      attrs.Index,
			Name:       attrs.Name,
			IsUp:       attrs.Flags&net.FlagUp != 0,
			IsLoopback: attrs.Flags&net.FlagLoopback != 0,
		}

		// Add IP addresses if requested
		if includeIPs {
			addrs, err := netlink.AddrList(iface.Link, netlink.FAMILY_ALL)
			if err == nil {
				info.IPAddresses = make([]string, 0, len(addrs))
				for _, addr := range addrs {
					info.IPAddresses = append(info.IPAddresses, addr.IPNet.String())
				}
			}
		}

		// Add Keenetic metadata if available
		if keeneticIfaces != nil {
			if keeneticIface, ok := keeneticIfaces[attrs.Name]; ok {
				info.KeeneticID = keeneticIface.ID
				info.KeeneticDescription = keeneticIface.Description
				info.KeeneticLink = keeneticIface.Link
				info.KeeneticConnected = keeneticIface.Connected
			}
		}

		result = append(result, info)
	}

	return result, nil
}

// Color constants for CLI formatting
const (
	colorReset = "\033[0m"
	colorCyan  = "\033[0;36m"
	colorGreen = "\033[32m"
	colorRed   = "\033[0;31m"
)

// FormatInterfacesForCLI returns a formatted string representation for CLI output.
func (s *InterfaceService) FormatInterfacesForCLI(interfaces []InterfaceInfo) string {
	var sb strings.Builder

	for _, iface := range interfaces {
		if iface.KeeneticID != "" {
			// Has Keenetic metadata
			sb.WriteString(fmt.Sprintf("%d. %s%s%s (%s%s%s / \"%s\") (%sup%s=%s%v%s %slink%s=%s%s%s %sconnected%s=%s%s%s)\n",
				iface.Index,
				colorCyan, iface.Name, colorReset,
				colorCyan, iface.KeeneticID, colorReset,
				iface.KeeneticDescription,
				colorCyan, colorReset,
				colorForBool(iface.IsUp), iface.IsUp, colorReset,
				colorCyan, colorReset,
				colorForString(iface.KeeneticLink, keenetic.KeeneticLinkUp), iface.KeeneticLink, colorReset,
				colorCyan, colorReset,
				colorForString(iface.KeeneticConnected, keenetic.KeeneticConnected), iface.KeeneticConnected, colorReset))
		} else {
			// No Keenetic metadata
			sb.WriteString(fmt.Sprintf("%d. %s%s%s (%sup%s=%s%v%s)\n",
				iface.Index,
				colorCyan, iface.Name, colorReset,
				colorCyan, colorReset,
				colorForBool(iface.IsUp), iface.IsUp, colorReset))
		}

		// Print IP addresses
		for _, ip := range iface.IPAddresses {
			family := "IPv4"
			if strings.Contains(ip, ":") {
				family = "IPv6"
			}
			sb.WriteString(fmt.Sprintf("  IP Address (%s): %s\n", family, ip))
		}
	}

	return sb.String()
}

func colorForBool(value bool) string {
	if value {
		return colorGreen
	}
	return colorRed
}

func colorForString(actual, expected string) string {
	if actual == expected {
		return colorGreen
	}
	return colorRed
}
