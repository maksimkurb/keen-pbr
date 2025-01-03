package networking

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/vishvananda/netlink"
	"golang.org/x/sys/unix"
	"log"
	"net"
)

const DEFAULT_ROUTE_METRIC = 100
const BLACKHOLE_ROUTE_METRIC = 200

type IpRoute struct {
	*netlink.Route
}

func (r *IpRoute) String() string {
	from := "all"
	if r.Src != nil && r.Src.String() != "<nil>" {
		from = r.Src.String()
	}

	to := "all"
	if r.Dst != nil && r.Dst.String() != "<nil>" {
		to = r.Dst.String()
	}

	linkName := "<nil>"
	if r.LinkIndex > 0 {
		if link, err := netlink.LinkByIndex(r.LinkIndex); err != nil {
			linkName = "<err: " + err.Error() + ">"
		} else {
			linkName = link.Attrs().Name
		}
	}

	return fmt.Sprintf("table %d: src=%s dst=%s -> dev %s (idx=%d)",
		r.Table, from, to, linkName, r.LinkIndex)
}

func BuildDefaultRoute(ipFamily config.IpFamily, iface Interface, table int) *IpRoute {
	ipr := netlink.Route{}

	ipr.Table = table
	ipr.LinkIndex = iface.Link.Attrs().Index
	ipr.Priority = DEFAULT_ROUTE_METRIC
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
	return &IpRoute{&ipr}
}

func BuildBlackholeRoute(ipFamily config.IpFamily, table int) *IpRoute {
	ipr := netlink.Route{}

	ipr.Table = table
	ipr.Priority = BLACKHOLE_ROUTE_METRIC
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
	return &IpRoute{&ipr}
}

func (ipr *IpRoute) Add() error {
	log.Printf("Adding IP route [%v]", ipr)
	if err := netlink.RouteAdd(ipr.Route); err != nil {
		log.Printf("Failed to add IP route [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRoute) AddIfNotExists() error {
	if exists, err := ipr.IsExists(); err != nil {
		return err
	} else {
		if !exists {
			return ipr.Add()
		}
	}
	return nil
}

func (ipr *IpRoute) IsExists() (bool, error) {
	if filtered, err := netlink.RouteListFiltered(ipr.Family, ipr.Route, netlink.RT_FILTER_TABLE|netlink.RT_FILTER_OIF); err != nil {
		log.Printf("Checking if IP route exists [%v] is failed: %v", ipr, err)
		return false, err
	} else {
		if len(filtered) > 0 {
			log.Printf("Checking if IP route exists [%v]: YES", ipr)
			return true, nil
		}
	}

	log.Printf("Checking if IP route exists [%v]: NO", ipr)
	return false, nil
}

func (ipr *IpRoute) Del() error {
	log.Printf("Deleting IP route [%v]", ipr)
	if err := netlink.RouteDel(ipr.Route); err != nil {
		log.Printf("Failed to delete IP route [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRoute) DelIfExists() error {
	if exists, err := ipr.IsExists(); err != nil {
		return err
	} else {
		if exists {
			return ipr.Del()
		}
	}
	return nil
}

//func DelIpRouteTable(table int) error {
//	log.Printf("Deleting IP route table [%d]", table)
//	routes, err := netlink.RouteListFiltered(netlink.FAMILY_ALL, &netlink.Route{Table: table}, netlink.RT_FILTER_TABLE)
//	if err != nil {
//		return err
//	}
//
//	for _, route := range routes {
//		if err := netlink.RouteDel(&route); err != nil {
//			return err
//		}
//	}
//
//	return nil
//}

func ListRoutesInTable(table int) ([]*IpRoute, error) {
	log.Printf("Listing all routes in the routing table %d", table)
	routes, err := netlink.RouteListFiltered(netlink.FAMILY_ALL, &netlink.Route{Table: table}, netlink.RT_FILTER_TABLE)
	if err != nil {
		log.Printf("Failed to list routes for table %d: %v", table, err)
		return nil, err
	}

	var ipRoutes []*IpRoute
	for _, route := range routes {
		ipRoutes = append(ipRoutes, &IpRoute{&route})
	}

	return ipRoutes, nil
}
