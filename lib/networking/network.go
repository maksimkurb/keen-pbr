package networking

import (
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/keenetic"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/vishvananda/netlink"
	"golang.org/x/sys/unix"
	"net"
)

func ApplyNetworkConfiguration(config *config.Config, onlyRoutingForInterface *string) (bool, error) {
	log.Infof("Applying network configuration.")

	appliedAtLeastOnce := false

	for _, ipset := range config.Ipset {
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

func applyIpsetNetworkConfiguration(ipset *config.IpsetConfig, useKeeneticAPI bool) error {
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
			ipset.Routing.FwMark, ipset.IpsetName, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)

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

func addDefaultGatewayRoute(ipset *config.IpsetConfig, chosenIface *Interface) error {
	log.Infof("Adding default gateway ip route dev=%s to table=%d", chosenIface.Attrs().Name, ipset.Routing.IpRouteTable)
	ipRoute := BuildDefaultRoute(ipset.IpVersion, *chosenIface, ipset.Routing.IpRouteTable)
	if err := ipRoute.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}

func addBlackholeRoute(ipset *config.IpsetConfig) error {
	log.Infof("Adding blackhole ip route to table=%d to prevent packets leakage (kill-switch)", ipset.Routing.IpRouteTable)
	route := BuildBlackholeRoute(ipset.IpVersion, ipset.Routing.IpRouteTable)
	if err := route.AddIfNotExists(); err != nil {
		return err
	}
	return nil
}

func BuildIPRuleForIpset(ipset *config.IpsetConfig) *IpRule {
	return BuildRule(ipset.IpVersion, ipset.Routing.FwMark, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)
}

func ChooseBestInterface(ipset *config.IpsetConfig, useKeeneticAPI bool, keeneticIfaces map[string]keenetic.Interface) (*Interface, error) {
	var chosenIface *Interface = nil

	log.Infof("Choosing best interface for ipset \"%s\" from the following list: %v", ipset.IpsetName, ipset.Routing.Interfaces)
	for _, interfaceName := range ipset.Routing.Interfaces {
		if iface, err := GetInterface(interfaceName); err != nil {
			return nil, err
		} else {
			addrs, addrsErr := netlink.AddrList(iface, netlink.FAMILY_ALL)
			var keeneticIface *keenetic.Interface = nil
			if useKeeneticAPI && addrsErr == nil {
				for _, addr := range addrs {
					if val, ok := keeneticIfaces[addr.IPNet.String()]; ok {
						keeneticIface = &val
						break
					}
				}
			}

			attrs := iface.Attrs()
			up := attrs.Flags&net.FlagUp != 0

			if useKeeneticAPI {
				if up && keeneticIface != nil && keeneticIface.Connected == keenetic.KEENETIC_CONNECTED && chosenIface == nil {
					chosenIface = iface
				}

				if keeneticIface != nil {
					var chosen = "  "
					if chosenIface == iface {
						chosen = colorGreen + "->" + colorReset
					}

					log.Infof(" %s %s (idx=%d) (%s / \"%s\") up=%v link=%s connected=%s",
						chosen,
						attrs.Name,
						attrs.Index,
						keeneticIface.ID,
						keeneticIface.Description,
						up,
						keeneticIface.Link,
						keeneticIface.Connected)
				} else {
					log.Infof("    %s (idx=%d) (unknown) up=%v link=unknown connected=unknown", attrs.Name, attrs.Index, up)
				}
			} else {
				if up && chosenIface == nil {
					chosenIface = iface
				}

				var chosen = "  "
				if chosenIface == iface {
					chosen = colorGreen + "->" + colorReset
				}

				log.Infof(" %s %s (idx=%d) up=%v", chosen, attrs.Name, attrs.Index, up)
			}
		}
	}

	if chosenIface == nil {
		log.Warnf("Could not choose best interface for ipset %s: all configured interfaces are down", ipset.IpsetName)
	}

	return chosenIface, nil
}
