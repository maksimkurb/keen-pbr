package networking

import (
	"fmt"
	"net"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
	"golang.org/x/sys/unix"
)

const (
	BlackholeRouteMetric = 1000
	GatewayRouteMetric   = 500
	InterfaceRouteBase   = 100
)

type IPRoute struct {
	*netlink.Route
}

func (ipr *IPRoute) String() string {
	from := "all"
	if ipr.Src != nil && ipr.Src.String() != "<nil>" {
		from = ipr.Src.String()
	}

	to := "all"
	if ipr.Dst != nil && ipr.Dst.String() != "<nil>" {
		to = ipr.Dst.String()
	}

	linkName := "<nil>"
	if ipr.LinkIndex > 0 {
		if link, err := netlink.LinkByIndex(ipr.LinkIndex); err != nil {
			linkName = "<err: " + err.Error() + ">"
		} else {
			linkName = link.Attrs().Name
		}
	}

	return fmt.Sprintf("table %d: src=%s dst=%s -> dev %s (idx=%d) [metric:%d]",
		ipr.Table, from, to, linkName, ipr.LinkIndex, ipr.Priority)
}

func BuildDefaultRoute(ipFamily config.IPFamily, iface Interface, table int, ipsetInterfaceIndex int) *IPRoute {
	ipr := netlink.Route{}

	ipr.Table = table
	ipr.LinkIndex = iface.Link.Attrs().Index
	// Use ipset interface index (position in config) as metric
	// metric = 100 + position (0-based), so first interface = 100, second = 101, etc.
	ipr.Priority = InterfaceRouteBase + ipsetInterfaceIndex
	if ipFamily == config.Ipv6 {
		ipr.Family = netlink.FAMILY_V6
		ipr.Dst = &net.IPNet{
			IP:   net.IPv6zero,
			Mask: net.CIDRMask(0, 128),
		}
	} else {
		ipr.Family = netlink.FAMILY_V4
		ipr.Dst = &net.IPNet{
			IP:   net.IPv4zero,
			Mask: net.CIDRMask(0, 32),
		}
	}
	return &IPRoute{&ipr}
}

func BuildBlackholeRoute(ipFamily config.IPFamily, table int) *IPRoute {
	ipr := netlink.Route{}

	ipr.Table = table
	ipr.Priority = BlackholeRouteMetric
	ipr.Type = unix.RTN_BLACKHOLE
	if ipFamily == config.Ipv6 {
		ipr.Family = netlink.FAMILY_V6
		ipr.Dst = &net.IPNet{
			IP:   net.IPv6zero,
			Mask: net.CIDRMask(0, 128),
		}
	} else {
		ipr.Family = netlink.FAMILY_V4
		ipr.Dst = &net.IPNet{
			IP:   net.IPv4zero,
			Mask: net.CIDRMask(0, 32),
		}
	}
	return &IPRoute{&ipr}
}

func BuildDefaultRouteViaGateway(ipFamily config.IPFamily, gateway net.IP, table int) *IPRoute {
	ipr := netlink.Route{}

	ipr.Table = table
	// Use a fixed metric for gateway routes (lower than blackhole but higher than interface)
	ipr.Priority = GatewayRouteMetric
	ipr.Gw = gateway
	if ipFamily == config.Ipv6 {
		ipr.Family = netlink.FAMILY_V6
		ipr.Dst = &net.IPNet{
			IP:   net.IPv6zero,
			Mask: net.CIDRMask(0, 128),
		}
	} else {
		ipr.Family = netlink.FAMILY_V4
		ipr.Dst = &net.IPNet{
			IP:   net.IPv4zero,
			Mask: net.CIDRMask(0, 32),
		}
	}
	return &IPRoute{&ipr}
}

func (ipr *IPRoute) Add() error {
	log.Debugf("Adding IP route [%v]", ipr)
	if err := netlink.RouteAdd(ipr.Route); err != nil {
		log.Warnf("Failed to add IP route [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IPRoute) AddIfNotExists() (bool, error) {
	if exists, err := ipr.IsExists(); err != nil {
		return false, err
	} else {
		if !exists {
			if err := ipr.Add(); err != nil {
				return false, err
			}
			return true, nil
		}
	}
	return false, nil
}

func (ipr *IPRoute) IsExists() (bool, error) {
	var filters uint64
	if ipr.Type == unix.RTN_BLACKHOLE {
		// For blackhole routes, don't use OIF filter since they have no output interface
		filters = netlink.RT_FILTER_TABLE | netlink.RT_FILTER_TYPE
	} else {
		// For regular routes, use the existing filters
		filters = netlink.RT_FILTER_TABLE | netlink.RT_FILTER_OIF
	}

	if filtered, err := netlink.RouteListFiltered(ipr.Family, ipr.Route, filters); err != nil {
		log.Warnf("Checking if IP route exists [%v] is failed: %v", ipr, err)
		return false, err
	} else {
		if len(filtered) > 0 {
			log.Debugf("Checking if IP route exists [%v]: YES", ipr)
			return true, nil
		}
	}

	log.Debugf("Checking if IP route exists [%v]: NO", ipr)
	return false, nil
}

func (ipr *IPRoute) Del() error {
	log.Debugf("Deleting IP route [%v]", ipr)
	if err := netlink.RouteDel(ipr.Route); err != nil {
		log.Warnf("Failed to delete IP route [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IPRoute) DelIfExists() (bool, error) {
	if exists, err := ipr.IsExists(); err != nil {
		return false, err
	} else {
		if exists {
			if err := ipr.Del(); err != nil {
				return false, err
			}
			return true, nil
		}
	}
	return false, nil
}

func DelIPRouteTable(table int) error {
	log.Debugf("Deleting IP route table [%d]", table)
	routes, err := netlink.RouteListFiltered(netlink.FAMILY_ALL, &netlink.Route{Table: table}, netlink.RT_FILTER_TABLE)
	if err != nil {
		return err
	}

	for _, route := range routes {
		if err := netlink.RouteDel(&route); err != nil {
			return err
		}
	}

	return nil
}

func ListRoutesInTable(table int) ([]*IPRoute, error) {
	log.Debugf("Listing all routes in the routing table %d", table)
	routes, err := netlink.RouteListFiltered(netlink.FAMILY_ALL, &netlink.Route{Table: table}, netlink.RT_FILTER_TABLE)
	if err != nil {
		log.Warnf("Failed to list routes for table %d: %v", table, err)
		return nil, err
	}

	var ipRoutes []*IPRoute
	for _, route := range routes {
		copiedRoute := route
		ipRoutes = append(ipRoutes, &IPRoute{&copiedRoute})
	}

	return ipRoutes, nil
}
