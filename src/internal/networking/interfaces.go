package networking

import (
	"net"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/vishvananda/netlink"
)

// InterfaceLister provides access to Keenetic router interface information.
// This is a subset interface to avoid circular imports with domain package.
// Both *keenetic.Client and mock implementations satisfy this interface.
type InterfaceLister interface {
	GetInterfaces() (map[string]keenetic.Interface, error)
}

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
