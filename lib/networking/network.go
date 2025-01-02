package networking

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"log"
	"net"
)

func ApplyNetworkConfiguration(config *config.Config, onlyRoutingForInterface *string) error {
	log.Printf("Applying network configuration.")

	for _, ipset := range config.Ipset {
		if err := applyIpsetNetworkConfiguration(ipset, onlyRoutingForInterface); err != nil {
			return err
		}
	}

	return nil
}

func applyIpsetNetworkConfiguration(ipset *config.IpsetConfig, onlyRoutingForInterface *string) error {
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
		return nil
	}

	rule := BuildRule(ipset.IpVersion, ipset.Routing.FwMark, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)

	if err := rule.DelIfExists(); err != nil {
		return err
	}

	if err := DelIpRouteTable(ipset.Routing.IpRouteTable); err != nil {
		return err
	}

	log.Printf("Choosing best interface for ipset \"%s\" from the following list: %v", ipset.IpsetName, ipset.Routing.Interfaces)
	var chosenIface *Interface = nil

	for _, interfaceName := range ipset.Routing.Interfaces {
		if iface, err := GetInterface(interfaceName); err != nil {
			return err
		} else {
			attrs := iface.Attrs()
			up := attrs.Flags&net.FlagUp != 0

			if up && chosenIface == nil {
				chosenIface = iface
				log.Printf("  %s (idx=%d) up=%v <-- choosing it", attrs.Name, attrs.Index, up)
			} else {
				log.Printf("  %s (idx=%d) up=%v", attrs.Name, attrs.Index, up)
			}
		}
	}

	if chosenIface == nil {
		return fmt.Errorf("failed to choose interface for ipset %s, all configured interfaces are down", ipset.IpsetName)
	}

	log.Printf("Adding IP rule to forward all packets with fwmark=%d (ipset=%s) to table=%d (priority=%d)",
		ipset.Routing.FwMark, ipset.IpsetName, ipset.Routing.IpRouteTable, ipset.Routing.IpRulePriority)

	if err := rule.Add(); err != nil {
		return err
	}

	log.Printf("Adding default IP route dev=%s to table=%d", chosenIface.Attrs().Name, ipset.Routing.IpRouteTable)
	route := BuildDefaultRoute(ipset.IpVersion, *chosenIface, ipset.Routing.IpRouteTable)
	if err := route.Add(); err != nil {
		return err
	}

	return nil
}
