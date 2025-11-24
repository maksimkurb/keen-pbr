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
	return ComponentTypeDNSRedirect
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

	// Check if chain has any rules (at least one redirect rule should exist)
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

	// For IsExists check, we only verify that rules exist (at least one)
	// The exact count may vary based on transient interfaces
	// CreateIfNotExists will refresh rules if needed
	if len(actualRules) == 0 {
		return false, nil
	}

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

	// Get list of interfaces instead of addresses
	links, err := netlink.LinkList()
	if err != nil {
		return fmt.Errorf("failed to list links: %w", err)
	}

	// Create a set of unique interface names to avoid duplicates
	interfaceSet := make(map[string]bool)
	for _, link := range links {
		ifName := link.Attrs().Name
		// Skip loopback interface
		if ifName == "lo" {
			continue
		}
		interfaceSet[ifName] = true
	}

	// Add rules for each interface
	// Match on incoming interface + destination port 53, redirect to target port
	// This works for all IP addresses (IPv4, global IPv6, link-local IPv6)
	for ifName := range interfaceSet {
		udpRule := []string{
			"-i", ifName,
			"-p", "udp",
			"--dport", strconv.Itoa(dnsSourcePort),
			"-j", "REDIRECT",
			"--to-ports", strconv.Itoa(int(c.targetPort)),
		}
		if err := ipt.AppendUnique("nat", dnsRedirectChainName, udpRule...); err != nil {
			return fmt.Errorf("failed to add UDP rule for %s: %w", ifName, err)
		}

		tcpRule := []string{
			"-i", ifName,
			"-p", "tcp",
			"--dport", strconv.Itoa(dnsSourcePort),
			"-j", "REDIRECT",
			"--to-ports", strconv.Itoa(int(c.targetPort)),
		}
		if err := ipt.AppendUnique("nat", dnsRedirectChainName, tcpRule...); err != nil {
			return fmt.Errorf("failed to add TCP rule for %s: %w", ifName, err)
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
