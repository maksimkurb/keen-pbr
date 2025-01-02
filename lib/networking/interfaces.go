package networking

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/vishvananda/netlink"
	"net"
)

const colorCyan = "\033[0;36m"
const colorGreen = "\033[0;32m"
const colorRed = "\033[0;31m"
const colorReset = "\033[0m"

type Interface struct {
	netlink.Link
}

func GetInterface(interfaceName string) (*Interface, error) {
	link, err := netlink.LinkByName(interfaceName)
	if err != nil {
		return nil, err
	}
	return &Interface{link}, nil
}

func GetInterfaceList() ([]Interface, error) {
	links, err := netlink.LinkList()
	if err != nil {
		return nil, err
	}
	var interfaces []Interface
	for _, link := range links {
		interfaces = append(interfaces, Interface{link})
	}
	return interfaces, nil
}

func PrintInterfaces(ifaces []Interface, printIPs bool) {
	for _, iface := range ifaces {
		attrs := iface.Attrs()

		up := attrs.Flags&net.FlagUp != 0
		upColor := colorRed
		if up {
			upColor = colorGreen
		}

		fmt.Printf("%d. %s%s%s (%sup%s=%s%v%s)\n",
			attrs.Index,
			colorCyan, attrs.Name, colorReset,
			colorCyan, colorReset,
			upColor, up, colorReset)
		if printIPs {
			if addrs, err := netlink.AddrList(iface, netlink.FAMILY_ALL); err != nil {
				fmt.Printf("failed to get addresses for interface %s: %v", attrs.Name, err)
			} else {
				for _, addr := range addrs {
					fmt.Printf("  IP Address (IPv%d): %v\n", getIPFamily(addr.IP), addr.IPNet)
				}
			}
		}
	}
}

func getIPFamily(ip net.IP) config.IpFamily {
	if len(ip) <= net.IPv4len {
		return config.Ipv4
	}
	if ip.To4() != nil {
		return config.Ipv4
	}
	return config.Ipv6
}

func (iface *Interface) IsUp() bool {
	return iface.Attrs().Flags&net.FlagUp != 0
}
func (iface *Interface) IsLoopback() bool {
	return iface.Attrs().Flags&net.FlagLoopback != 0
}

func (iface *Interface) AddrsIps() ([]net.IP, error) {
	addrs, err := netlink.AddrList(iface.Link, netlink.FAMILY_ALL)
	if err != nil {
		return nil, err
	}
	var ips []net.IP
	for _, addr := range addrs {
		ips = append(ips, addr.IP)
	}
	return ips, nil
}
