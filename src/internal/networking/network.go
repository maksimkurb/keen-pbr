package networking

import (
	"net"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
	"golang.org/x/sys/unix"
)

func ApplyNetworkConfiguration(config *config.Config, onlyRoutingForInterface *string) (bool, error) {
	log.Infof("Applying network configuration.")

	appliedAtLeastOnce := false

	for _, ipset := range config.IPSets {
		shouldRoute := false
		if onlyRoutingForInterface == nil || *onlyRoutingForInterface == "" {
			shouldRoute = true
		} else {
			for _, interfaceName := range ipset.Routing.Interfaces {
				if interfaceName == *onlyRoutingForInterface {
					shouldRoute = true
					break
				}
			}
		}

		if !shouldRoute {
			continue
		}

		appliedAtLeastOnce = true
		if err := applyIpsetNetworkConfiguration(ipset, *config.General.UseKeeneticAPI); err != nil {
			return false, err
		}
	}

	return appliedAtLeastOnce, nil
}

func applyIpsetNetworkConfiguration(ipset *config.IPSetConfig, useKeeneticAPI bool) error {
	var keeneticIfaces map[string]keenetic.Interface = nil
	if useKeeneticAPI {
		var err error
		keeneticIfaces, err = keenetic.RciShowInterfaceMappedByIPNet()
		if err != nil {
			log.Warnf("failed to query Keenetic API: %v", err)
		}
	}

	ipRule := BuildIPRuleForIpset(ipset)
	ipTableRules, err := BuildIPTablesForIpset(ipset)
	if err != nil {
		return err
	}

	if !ipset.Routing.KillSwitch {
		if err := ipRule.DelIfExists(); err != nil {
			return err
		}
		if err := ipTableRules.DelIfExists(); err != nil {
			return err
		}
	}

	blackholePresent := false

	if routes, err := ListRoutesInTable(ipset.Routing.IpRouteTable); err != nil {
		return err
	} else {
		// Cleanup all routes (except blackhole route if kill switch is enabled)
		for _, route := range routes {
			if ipset.Routing.KillSwitch && route.Type&unix.RTN_BLACKHOLE != 0 {
				blackholePresent = true
				continue
			}

			if err := route.DelIfExists(); err != nil {
				return err
			}
		}
	}

	var chosenIface *Interface = nil
	chosenIface, err = ChooseBestInterface(ipset, useKeeneticAPI, keeneticIfaces)
	if err != nil {
		return err
	}

	if ipset.Routing.KillSwitch || chosenIface != nil {
		log.Infof("Adding ip rule to forward all packets with fwmark=%d (ipset=%s) to table=%d (priority=%d)",
			ipset.Routing.FwMark, ipset.IPSetName, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)

		if err := ipRule.AddIfNotExists(); err != nil {
			return err
		}

		if err := ipTableRules.AddIfNotExists(); err != nil {
			return err
		}
	}

	if ipset.Routing.KillSwitch && !blackholePresent {
		if err := addBlackholeRoute(ipset); err != nil {
			return err
		}
	}

	if chosenIface != nil {
		if err := addDefaultGatewayRoute(ipset, chosenIface); err != nil {
			return err
		}
	}

	return nil
}

func addDefaultGatewayRoute(ipset *config.IPSetConfig, chosenIface *Interface) error {
	log.Infof("Adding default gateway ip route dev=%s to table=%d", chosenIface.Attrs().Name, ipset.Routing.IpRouteTable)
	ipRoute := BuildDefaultRoute(ipset.IPVersion, *chosenIface, ipset.Routing.IpRouteTable)
	if err := ipRoute.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}

func addBlackholeRoute(ipset *config.IPSetConfig) error {
	log.Infof("Adding blackhole ip route to table=%d to prevent packets leakage (kill-switch)", ipset.Routing.IpRouteTable)
	route := BuildBlackholeRoute(ipset.IPVersion, ipset.Routing.IpRouteTable)
	if err := route.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}

func BuildIPRuleForIpset(ipset *config.IPSetConfig) *IpRule {
	return BuildRule(ipset.IPVersion, ipset.Routing.FwMark, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)
}

func ChooseBestInterface(ipset *config.IPSetConfig, useKeeneticAPI bool, keeneticIfaces map[string]keenetic.Interface) (*Interface, error) {
	var chosenIface *Interface

	log.Infof("Choosing best interface for ipset \"%s\" from the following list: %v", ipset.IPSetName, ipset.Routing.Interfaces)
	
	for _, interfaceName := range ipset.Routing.Interfaces {
		iface, err := GetInterface(interfaceName)
		if err != nil {
			log.Errorf("Failed to get interface \"%s\" status: %v", interfaceName, err)
			continue
		}

		attrs := iface.Attrs()
		up := attrs.Flags&net.FlagUp != 0
		keeneticIface := getKeeneticInterface(iface, useKeeneticAPI, keeneticIfaces)

		// Check if this interface should be chosen
		if chosenIface == nil && isInterfaceUsable(up, keeneticIface, useKeeneticAPI) {
			chosenIface = iface
		}

		// Log interface status
		logInterfaceStatus(iface, up, keeneticIface, chosenIface == iface, useKeeneticAPI)
	}

	if chosenIface == nil {
		log.Warnf("Could not choose best interface for ipset %s: all configured interfaces are down", ipset.IPSetName)
	}

	return chosenIface, nil
}

// getKeeneticInterface finds the Keenetic interface info for the given system interface
func getKeeneticInterface(iface *Interface, useKeeneticAPI bool, keeneticIfaces map[string]keenetic.Interface) *keenetic.Interface {
	if !useKeeneticAPI {
		return nil
	}

	addrs, err := netlink.AddrList(iface, netlink.FAMILY_ALL)
	if err != nil {
		return nil
	}

	for _, addr := range addrs {
		if val, ok := keeneticIfaces[addr.IPNet.String()]; ok {
			return &val
		}
	}
	return nil
}

// isInterfaceUsable determines if an interface can be used for routing
func isInterfaceUsable(up bool, keeneticIface *keenetic.Interface, useKeeneticAPI bool) bool {
	if !up {
		return false
	}

	if !useKeeneticAPI {
		return true
	}

	// With Keenetic API: interface is usable if it's up and either:
	// 1. Keenetic status shows connected=yes, or
	// 2. Keenetic status is unknown (interface not found in API)
	return keeneticIface == nil || keeneticIface.Connected == keenetic.KEENETIC_CONNECTED
}

// logInterfaceStatus logs the status of an interface in a consistent format
func logInterfaceStatus(iface *Interface, up bool, keeneticIface *keenetic.Interface, isChosen bool, useKeeneticAPI bool) {
	attrs := iface.Attrs()
	chosen := "  "
	if isChosen {
		chosen = colorGreen + "->" + colorReset
	}

	if useKeeneticAPI {
		if keeneticIface != nil {
			log.Infof(" %s %s (idx=%d) (%s / \"%s\") up=%v link=%s connected=%s",
				chosen, attrs.Name, attrs.Index,
				keeneticIface.ID, keeneticIface.Description,
				up, keeneticIface.Link, keeneticIface.Connected)
		} else {
			log.Infof(" %s %s (idx=%d) (unknown) up=%v link=unknown connected=unknown",
				chosen, attrs.Name, attrs.Index, up)
		}
	} else {
		log.Infof(" %s %s (idx=%d) up=%v", chosen, attrs.Name, attrs.Index, up)
	}
}
