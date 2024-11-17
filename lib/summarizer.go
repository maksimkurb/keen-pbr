package lib

import (
	"net"
	"sort"
)

func SummarizeIPv4(networks []string) []string {
	if len(networks) == 0 {
		return nil
	}

	// Parse and sort networks
	var parsedNets []*net.IPNet
	for _, netStr := range networks {
		_, ipnet, err := net.ParseCIDR(netStr)
		if err != nil {
			continue
		}
		parsedNets = append(parsedNets, ipnet)
	}

	sort.Slice(parsedNets, func(i, j int) bool {
		return compareIP(parsedNets[i].IP, parsedNets[j].IP) < 0
	})

	// Merge networks
	var result []string
	if len(parsedNets) == 0 {
		return result
	}

	current := parsedNets[0]
	for i := 1; i < len(parsedNets); i++ {
		next := parsedNets[i]
		if canMergeNetworks(current, next) {
			if merged := mergeNetworks(current, next); merged != nil {
				current = merged
			}
		} else {
			result = append(result, current.String())
			current = next
		}
	}
	result = append(result, current.String())

	return result
}

func canMergeNetworks(a, b *net.IPNet) bool {
	ones1, bits1 := a.Mask.Size()
	ones2, bits2 := b.Mask.Size()

	if ones1 != ones2 || bits1 != bits2 {
		return false
	}

	// Check if networks are adjacent or overlapping
	last := incrementIP(lastIP(a))
	return compareIP(last, b.IP) >= 0
}

func mergeNetworks(a, b *net.IPNet) *net.IPNet {
	if !canMergeNetworks(a, b) {
		return nil
	}

	ones, bits := a.Mask.Size()
	newMask := net.CIDRMask(ones-1, bits)

	return &net.IPNet{
		IP:   a.IP.Mask(newMask),
		Mask: newMask,
	}
}

func incrementIP(ip net.IP) net.IP {
	newIP := make(net.IP, len(ip))
	copy(newIP, ip)

	for i := len(newIP) - 1; i >= 0; i-- {
		newIP[i]++
		if newIP[i] != 0 {
			break
		}
	}
	return newIP
}

func lastIP(ipnet *net.IPNet) net.IP {
	ip := make(net.IP, len(ipnet.IP))
	copy(ip, ipnet.IP)
	for i := range ip {
		ip[i] |= ^ipnet.Mask[i]
	}
	return ip
}

func compareIP(a, b net.IP) int {
	for i := range a {
		if a[i] != b[i] {
			return int(a[i]) - int(b[i])
		}
	}
	return 0
}
