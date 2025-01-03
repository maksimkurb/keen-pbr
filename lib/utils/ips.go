package utils

import (
	"fmt"
	"net"
)

func IPv4ToNetmask(ipStr, maskStr string) (*net.IPNet, error) {
	// Parse the IPv4 address
	ip := net.ParseIP(ipStr)
	if ip == nil || ip.To4() == nil {
		return nil, fmt.Errorf("invalid IPv4 address: %s", ipStr)
	}

	// Parse the IPv4 mask
	mask := net.ParseIP(maskStr)
	if mask == nil || mask.To4() == nil {
		return nil, fmt.Errorf("invalid IPv4 mask: %s", maskStr)
	}

	// Apply the mask to the IP address
	ipNet := &net.IPNet{
		IP:   ip,
		Mask: net.IPMask(mask.To4()),
	}

	return ipNet, nil
}

func IPv6ToNetmask(ipStr string, prefixLen int) (*net.IPNet, error) {
	// Parse the IPv6 address
	ip := net.ParseIP(ipStr)
	if ip == nil || ip.To16() == nil {
		return nil, fmt.Errorf("invalid IPv6 address: %s", ipStr)
	}

	// Create the prefix mask
	mask := net.CIDRMask(prefixLen, 128)
	if mask == nil {
		return nil, fmt.Errorf("invalid prefix length: %d", prefixLen)
	}

	// Combine the address and the mask into an IPNet
	ipNet := &net.IPNet{
		IP:   ip.Mask(mask),
		Mask: mask,
	}

	return ipNet, nil
}
