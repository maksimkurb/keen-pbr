package networking

import (
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
	"net"
)

const colorCyan = "\033[0;36m"
const colorRed = "\033[0;31m"

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

func PrintInterfaces(ifaces []Interface, printIPs bool, keeneticClient *keenetic.Client) {
	var keeneticIfaces map[string]keenetic.Interface = nil
	var err error
	if keeneticClient != nil {
		keeneticIfaces, err = keeneticClient.GetInterfaces()
		if err != nil {
			log.Warnf("failed to get Keenetic interfaces: %v", err)
		}
	}

	for _, iface := range ifaces {
		attrs := iface.Attrs()

		up := attrs.Flags&net.FlagUp != 0

		addrs, addrsErr := netlink.AddrList(iface, netlink.FAMILY_ALL)
		var keeneticIface *keenetic.Interface = nil
		if keeneticIfaces != nil {
			// Try direct lookup by system name
			if val, ok := keeneticIfaces[attrs.Name]; ok {
				keeneticIface = &val
			}
		}

		if keeneticIface != nil {
			fmt.Printf("%d. %s%s%s (%s%s%s / \"%s\") (%sup%s=%s%v%s %slink%s=%s%s%s %sconnected%s=%s%s%s)\n",
				attrs.Index,
				colorCyan, attrs.Name, colorReset,
				colorCyan, keeneticIface.ID, colorReset,
				keeneticIface.Description,
				colorCyan, colorReset,
				colorGreenIfTrue(up), up, colorReset,
				colorCyan, colorReset,
				colorGreenIfEquals(keeneticIface.Link, keenetic.KEENETIC_LINK_UP), keeneticIface.Link, colorReset,
				colorCyan, colorReset,
				colorGreenIfEquals(keeneticIface.Connected, keenetic.KEENETIC_CONNECTED), keeneticIface.Connected, colorReset)
		} else {
			fmt.Printf("%d. %s%s%s (%sup%s=%s%v%s)\n",
				attrs.Index,
				colorCyan, attrs.Name, colorReset,
				colorCyan, colorReset,
				colorGreenIfTrue(up), up, colorReset)
		}

		if printIPs {
			if addrsErr != nil {
				fmt.Printf("failed to get addresses for interface %s: %v", attrs.Name, addrsErr)
			} else {
				for _, addr := range addrs {
					fmt.Printf("  IP Address (IPv%d): %v\n", getIPFamily(addr.IP), addr.IPNet)
				}
			}
		}
	}
}

func colorGreenIfEquals(actual string, expected string) string {
	if actual == expected {
		return colorGreen
	}
	return colorRed
}

func colorGreenIfTrue(actual bool) string {
	if actual {
		return colorGreen
	}
	return colorRed
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
