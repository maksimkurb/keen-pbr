package networking

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/coreos/go-iptables/iptables"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
)

const (
	// dnsRedirectChainName is the name of the iptables chain for DNS redirection.
	dnsRedirectChainName = "KEEN_PBR_DNS"

	// dnsSourcePort is the DNS port we're redirecting from.
	dnsSourcePort = 53
)

// DNSRedirectComponent manages iptables rules for DNS traffic redirection.
// It creates DNAT rules to intercept DNS traffic destined for the router
// and redirect it to the DNS proxy IP and port.
type DNSRedirectComponent struct {
	listenAddr string
	targetIPv4 string
	targetIPv6 string
	targetPort uint16
	ipt4       *iptables.IPTables
	ipt6       *iptables.IPTables
}

// NewDNSRedirectComponent creates a new component for DNS redirection.
// listenAddr is the address the DNS proxy listens on (e.g., "[::]" for dual-stack).
// When listenAddr is "[::]" (dual-stack), redirects to localhost (::1 for IPv6, 127.0.0.1 for IPv4).
func NewDNSRedirectComponent(listenAddr string, targetPort uint16) (*DNSRedirectComponent, error) {
	ipt4, err := iptables.NewWithProtocol(iptables.ProtocolIPv4)
	if err != nil {
		return nil, fmt.Errorf("failed to create iptables (IPv4): %w", err)
	}

	ipt6, err := iptables.NewWithProtocol(iptables.ProtocolIPv6)
	if err != nil {
		// IPv6 might not be available, that's okay
		log.Debugf("IPv6 iptables not available: %v", err)
		ipt6 = nil
	}

	// Strip brackets from listenAddr if present (e.g., "[::] -> "::")
	addr := strings.Trim(listenAddr, "[]")

	// Determine target IPs based on listenAddr
	var targetIPv4, targetIPv6 string
	if addr == "::" {
		// Dual-stack: redirect to localhost
		targetIPv4 = "127.0.0.1"
		targetIPv6 = "::1"
	} else {
		// Specific address: use it for both (will be filtered by IP version in createChainAndRules)
		targetIPv4 = addr
		targetIPv6 = addr
	}

	return &DNSRedirectComponent{
		listenAddr: listenAddr,
		targetIPv4: targetIPv4,
		targetIPv6: targetIPv6,
		targetPort: targetPort,
		ipt4:       ipt4,
		ipt6:       ipt6,
	}, nil
}

// IsExists checks if the DNS redirection rules currently exist and are correct.
func (c *DNSRedirectComponent) IsExists() (bool, error) {
	// Check IPv4
	if c.ipt4 != nil {
		exists, err := c.checkChainAndRules(c.ipt4)
		if err != nil {
			return false, fmt.Errorf("IPv4 check failed: %w", err)
		}
		if !exists {
			return false, nil
		}
	}

	// Check IPv6
	if c.ipt6 != nil {
		exists, err := c.checkChainAndRules(c.ipt6)
		if err != nil {
			return false, fmt.Errorf("IPv6 check failed: %w", err)
		}
		if !exists {
			return false, nil
		}
	}

	return true, nil
}

// ShouldExist determines if this component should be present.
// For DNS redirect, if the component is created, it implies it should exist
// (usually controlled by higher level logic enabling/disabling the feature).
func (c *DNSRedirectComponent) ShouldExist() bool {
	return true
}

// CreateIfNotExists creates the component if it doesn't exist.
func (c *DNSRedirectComponent) CreateIfNotExists() error {
	// We always recreate to ensure rules match current interfaces
	if err := c.DeleteIfExists(); err != nil {
		return fmt.Errorf("failed to cleanup before creation: %w", err)
	}

	// Get all local addresses
	addresses, err := getLocalAddresses()
	if err != nil {
		return fmt.Errorf("failed to get local addresses: %w", err)
	}

	// Create rules for IPv4
	if c.ipt4 != nil {
		if err := c.createChainAndRules(c.ipt4, addresses); err != nil {
			return fmt.Errorf("failed to create IPv4 rules: %w", err)
		}
	}

	// Create rules for IPv6
	if c.ipt6 != nil {
		if err := c.createChainAndRules(c.ipt6, addresses); err != nil {
			return fmt.Errorf("failed to create IPv6 rules: %w", err)
		}
	}

	return nil
}

// DeleteIfExists removes the component if it exists.
func (c *DNSRedirectComponent) DeleteIfExists() error {
	if c.ipt4 != nil {
		c.deleteChainAndRules(c.ipt4)
	}

	if c.ipt6 != nil {
		c.deleteChainAndRules(c.ipt6)
	}

	return nil
}

// GetType returns the component type.
func (c *DNSRedirectComponent) GetType() ComponentType {
	return ComponentTypeIPTables
}

// GetIPSetName returns the associated IPSet name.
// DNS redirect is not associated with a specific IPSet, so returns empty.
func (c *DNSRedirectComponent) GetIPSetName() string {
	return ""
}

// GetDescription returns human-readable description.
func (c *DNSRedirectComponent) GetDescription() string {
	return fmt.Sprintf("DNS redirection rules (port 53 -> %s/%s:%d)", c.targetIPv4, c.targetIPv6, c.targetPort)
}

// Helper methods

func (c *DNSRedirectComponent) checkChainAndRules(ipt *iptables.IPTables) (bool, error) {
	// Check if chain exists
	exists, err := ipt.ChainExists("nat", dnsRedirectChainName)
	if err != nil {
		return false, err
	}
	if !exists {
		return false, nil
	}

	// Check if linked to PREROUTING
	rules, err := ipt.List("nat", "PREROUTING")
	if err != nil {
		return false, err
	}
	linked := false
	for _, rule := range rules {
		if strings.Contains(rule, "-j "+dnsRedirectChainName) {
			linked = true
			break
		}
	}
	if !linked {
		return false, nil
	}

	// Check if rules match current addresses
	// This is a strict check: if addresses changed, we consider it "not exists" (needs update)
	currentAddresses, err := getLocalAddresses()
	if err != nil {
		return false, err
	}

	existingRules, err := ipt.List("nat", dnsRedirectChainName)
	if err != nil {
		return false, err
	}

	// Filter out chain creation rule ("-N KEEN_PBR_DNS")
	var actualRules []string
	for _, rule := range existingRules {
		if strings.HasPrefix(rule, "-A") {
			actualRules = append(actualRules, rule)
		}
	}

	// Calculate expected rules count
	expectedCount := 0
	for _, addr := range currentAddresses {
		isIPv4 := addr.IP.To4() != nil
		isIPv6 := ipt.Proto() == iptables.ProtocolIPv6

		if (isIPv4 && !isIPv6) || (!isIPv4 && isIPv6) {
			expectedCount += 2 // TCP + UDP
		}
	}

	// Simple check: count mismatch means update needed
	if len(actualRules) != expectedCount {
		return false, nil
	}

	// TODO: Deep comparison of rules if needed, but count is a good proxy for "something changed"
	return true, nil
}

func (c *DNSRedirectComponent) createChainAndRules(ipt *iptables.IPTables, addresses []netlink.Addr) error {
	// Create chain
	if err := ipt.NewChain("nat", dnsRedirectChainName); err != nil {
		// Check if chain already exists
		if eerr, ok := err.(*iptables.Error); !(ok && eerr.ExitStatus() == 1) {
			return fmt.Errorf("failed to create chain: %w", err)
		}
	}

	// Determine IP version
	isIPv6Table := ipt.Proto() == iptables.ProtocolIPv6

	// Add rules for each local address
	for _, addr := range addresses {
		// Skip addresses that don't match this IP version
		isIPv4 := addr.IP.To4() != nil

		if isIPv4 && isIPv6Table {
			continue
		}
		if !isIPv4 && !isIPv6Table {
			continue
		}

		// For IPv4: use REDIRECT (preserves original destination IP)
		// For IPv6: use DNAT with port-only destination
		if !isIPv6Table {
			// IPv4: REDIRECT --to-port
			udpRule := []string{
				"-p", "udp",
				"-d", addr.IP.String(),
				"--dport", strconv.Itoa(dnsSourcePort),
				"-j", "REDIRECT",
				"--to-port", strconv.Itoa(int(c.targetPort)),
			}
			if err := ipt.AppendUnique("nat", dnsRedirectChainName, udpRule...); err != nil {
				return fmt.Errorf("failed to add UDP rule: %w", err)
			}

			tcpRule := []string{
				"-p", "tcp",
				"-d", addr.IP.String(),
				"--dport", strconv.Itoa(dnsSourcePort),
				"-j", "REDIRECT",
				"--to-port", strconv.Itoa(int(c.targetPort)),
			}
			if err := ipt.AppendUnique("nat", dnsRedirectChainName, tcpRule...); err != nil {
				return fmt.Errorf("failed to add TCP rule: %w", err)
			}
		} else {
			// IPv6: DNAT --to-destination :<port>
			udpRule := []string{
				"-p", "udp",
				"-d", addr.IP.String(),
				"--dport", strconv.Itoa(dnsSourcePort),
				"-j", "DNAT",
				"--to-destination", fmt.Sprintf(":%d", c.targetPort),
			}
			if err := ipt.AppendUnique("nat", dnsRedirectChainName, udpRule...); err != nil {
				return fmt.Errorf("failed to add UDP rule: %w", err)
			}

			tcpRule := []string{
				"-p", "tcp",
				"-d", addr.IP.String(),
				"--dport", strconv.Itoa(dnsSourcePort),
				"-j", "DNAT",
				"--to-destination", fmt.Sprintf(":%d", c.targetPort),
			}
			if err := ipt.AppendUnique("nat", dnsRedirectChainName, tcpRule...); err != nil {
				return fmt.Errorf("failed to add TCP rule: %w", err)
			}
		}
	}

	// Link chain to PREROUTING
	if err := ipt.InsertUnique("nat", "PREROUTING", 1, "-j", dnsRedirectChainName); err != nil {
		return fmt.Errorf("failed to link chain: %w", err)
	}

	return nil
}

func (c *DNSRedirectComponent) deleteChainAndRules(ipt *iptables.IPTables) {
	// Unlink from PREROUTING
	if err := ipt.DeleteIfExists("nat", "PREROUTING", "-j", dnsRedirectChainName); err != nil {
		log.Debugf("Failed to unlink chain: %v", err)
	}

	// Clear chain
	if err := ipt.ClearChain("nat", dnsRedirectChainName); err != nil {
		log.Debugf("Failed to clear chain: %v", err)
	}

	// Delete chain
	if err := ipt.DeleteChain("nat", dnsRedirectChainName); err != nil {
		log.Debugf("Failed to delete chain: %v", err)
	}
}

// getLocalAddresses returns all local IP addresses.
func getLocalAddresses() ([]netlink.Addr, error) {
	links, err := netlink.LinkList()
	if err != nil {
		return nil, fmt.Errorf("failed to list links: %w", err)
	}

	var addresses []netlink.Addr
	for _, link := range links {
		addrs, err := netlink.AddrList(link, netlink.FAMILY_ALL)
		if err != nil {
			log.Debugf("Failed to get addresses for %s: %v", link.Attrs().Name, err)
			continue
		}

		for _, addr := range addrs {
			// Skip loopback addresses for redirection
			// (we want to intercept traffic to router's public IPs)
			if addr.IP.IsLoopback() {
				continue
			}
			addresses = append(addresses, addr)
		}
	}

	return addresses, nil
}
