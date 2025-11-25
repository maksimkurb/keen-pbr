package networking

import (
	"net"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// Ensure *keenetic.Client satisfies InterfaceLister
var _ InterfaceLister = (*keenetic.Client)(nil)

const (
	colorGreen = "\033[32m"
	colorReset = "\033[0m"
)

// InterfaceSelector handles the logic for selecting the best available network interface
// for routing traffic through VPN or proxy connections.
//
// The selector checks interface status both from the system (netlink) and from the
// Keenetic router API (if available) to determine which interface is currently usable.
type InterfaceSelector struct {
	keeneticClient InterfaceLister
}

// NewInterfaceSelector creates a new interface selector.
//
// The keeneticClient parameter can be nil if Keenetic API integration is not available.
// In this case, only system-level interface status will be checked.
func NewInterfaceSelector(keeneticClient InterfaceLister) *InterfaceSelector {
	return &InterfaceSelector{
		keeneticClient: keeneticClient,
	}
}

// ChooseBest selects the best available interface from the configured list.
//
// The selection process follows these rules:
// 1. Interfaces are checked in the order they appear in the config
// 2. The first interface that is UP and working is selected
// 3. If Keenetic API is available, the interface must also have connected=yes
// 4. If no interface is available, nil is returned (caller should add blackhole route)
//
// Returns the chosen interface and its index in the ipset's interface list (0-based).
// If no interface is chosen, returns (nil, -1, nil).
func (s *InterfaceSelector) ChooseBest(ipset *config.IPSetConfig) (*Interface, int, error) {
	var chosenIface *Interface
	var chosenIndex = -1

	// Get Keenetic interfaces if available
	var keeneticIfaces map[string]keenetic.Interface
	if s.keeneticClient != nil {
		var err error
		keeneticIfaces, err = s.keeneticClient.GetInterfaces()
		if err != nil {
			log.Warnf("Failed to query Keenetic API: %v", err)
		}
	}

	log.Debugf("Choosing best interface for ipset \"%s\" from the following list: %v",
		ipset.IPSetName, ipset.Routing.Interfaces)

	for idx, interfaceName := range ipset.Routing.Interfaces {
		iface, err := GetInterface(interfaceName)
		if err != nil {
			log.Errorf("Failed to get interface \"%s\" status: %v", interfaceName, err)
			continue
		}

		attrs := iface.Attrs()
		up := attrs.Flags&net.FlagUp != 0
		keeneticIface := s.getKeeneticInterface(iface, keeneticIfaces)

		// Check if this interface should be chosen
		if chosenIface == nil && s.IsUsable(iface, keeneticIface) {
			chosenIface = iface
			chosenIndex = idx
		}

		// Log interface status at debug level
		s.logInterfaceStatus(iface, up, keeneticIface, chosenIface == iface)
	}

	if chosenIface == nil {
		log.Debugf("Could not choose best interface for ipset %s: all configured interfaces are down",
			ipset.IPSetName)
	}

	return chosenIface, chosenIndex, nil
}

// IsUsable determines if an interface can be used for routing.
//
// An interface is considered usable if:
//  1. It is UP (system level)
//  2. Either:
//     a) Keenetic status is unknown (interface not found in API or API failed), OR
//     b) Keenetic status does NOT explicitly say connected=no
//
// Note: Empty connected field is treated as "unknown", not as "disconnected".
// This prevents route churn when Keenetic API returns incomplete data.
func (s *InterfaceSelector) IsUsable(iface *Interface, keeneticIface *keenetic.Interface) bool {
	attrs := iface.Attrs()
	up := attrs.Flags&net.FlagUp != 0

	if !up {
		return false
	}

	// Interface is usable if it's up and Keenetic doesn't explicitly say it's down.
	// Empty string (unknown status) is treated as usable - we trust the system status.
	// Only if connected="no" do we consider it unusable.
	return keeneticIface == nil || keeneticIface.Connected != "no"
}

// getKeeneticInterface finds the Keenetic interface info for the given system interface.
func (s *InterfaceSelector) getKeeneticInterface(iface *Interface, keeneticIfaces map[string]keenetic.Interface) *keenetic.Interface {
	if keeneticIfaces == nil {
		return nil
	}

	// Try direct lookup by system name
	if val, ok := keeneticIfaces[iface.Attrs().Name]; ok {
		return &val
	}

	return nil
}

// logInterfaceStatus logs the status of an interface in a consistent, readable format.
func (s *InterfaceSelector) logInterfaceStatus(iface *Interface, up bool, keeneticIface *keenetic.Interface, isChosen bool) {
	attrs := iface.Attrs()
	chosen := "  "
	if isChosen {
		chosen = colorGreen + "->" + colorReset
	}

	if keeneticIface != nil {
		log.Debugf(" %s %s (idx=%d) (%s / \"%s\") up=%v link=%s connected=%s",
			chosen, attrs.Name, attrs.Index,
			keeneticIface.ID, keeneticIface.Description,
			up, keeneticIface.Link, keeneticIface.Connected)
	} else {
		log.Debugf(" %s %s (idx=%d) up=%v", chosen, attrs.Name, attrs.Index, up)
	}
}
