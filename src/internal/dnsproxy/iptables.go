package dnsproxy

import (
	"fmt"
	"strconv"
	"sync"

	"github.com/coreos/go-iptables/iptables"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
)

const (
	// chainName is the name of the iptables chain for DNS redirection.
	chainName = "KEEN_PBR_DNS"

	// sourcePort is the DNS port we're redirecting from.
	sourcePort = 53
)

// IPTablesManager manages iptables rules for DNS traffic redirection.
// It creates REDIRECT rules to intercept DNS traffic destined for the router
// and redirect it to the DNS proxy port.
type IPTablesManager struct {
	mu sync.Mutex

	enabled    bool
	targetPort uint16
	addresses  []netlink.Addr

	ipt4 *iptables.IPTables
	ipt6 *iptables.IPTables
}

// NewIPTablesManager creates a new iptables manager for DNS redirection.
func NewIPTablesManager(targetPort uint16) (*IPTablesManager, error) {
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

	return &IPTablesManager{
		targetPort: targetPort,
		ipt4:       ipt4,
		ipt6:       ipt6,
	}, nil
}

// Enable enables DNS redirection by creating iptables rules.
// It gets the router's IP addresses and creates REDIRECT rules for DNS traffic.
func (m *IPTablesManager) Enable() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.enabled {
		return nil
	}

	// Get all local addresses
	addresses, err := getLocalAddresses()
	if err != nil {
		return fmt.Errorf("failed to get local addresses: %w", err)
	}
	m.addresses = addresses

	// First, clean up any existing rules
	m.disable()

	// Create chain and rules
	if err := m.createRules(); err != nil {
		m.disable()
		return err
	}

	m.enabled = true
	log.Infof("DNS redirection enabled (port %d -> %d)", sourcePort, m.targetPort)

	return nil
}

// Disable disables DNS redirection by removing iptables rules.
func (m *IPTablesManager) Disable() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if !m.enabled {
		return nil
	}

	if err := m.disable(); err != nil {
		return err
	}

	m.enabled = false
	log.Infof("DNS redirection disabled")

	return nil
}

// createRules creates the iptables chain and rules.
func (m *IPTablesManager) createRules() error {
	// Create rules for IPv4
	if m.ipt4 != nil {
		if err := m.createChainAndRules(m.ipt4); err != nil {
			return fmt.Errorf("failed to create IPv4 rules: %w", err)
		}
	}

	// Create rules for IPv6
	if m.ipt6 != nil {
		if err := m.createChainAndRules(m.ipt6); err != nil {
			return fmt.Errorf("failed to create IPv6 rules: %w", err)
		}
	}

	return nil
}

// createChainAndRules creates the chain and rules for a specific IP version.
func (m *IPTablesManager) createChainAndRules(ipt *iptables.IPTables) error {
	// Create chain
	if err := ipt.NewChain("nat", chainName); err != nil {
		// Check if chain already exists
		if eerr, ok := err.(*iptables.Error); !(ok && eerr.ExitStatus() == 1) {
			return fmt.Errorf("failed to create chain: %w", err)
		}
	}

	// Add rules for each local address
	for _, addr := range m.addresses {
		// Skip addresses that don't match this IP version
		isIPv4 := addr.IP.To4() != nil
		isIPv6 := ipt.Proto() == iptables.ProtocolIPv6

		if isIPv4 && isIPv6 {
			continue
		}
		if !isIPv4 && !isIPv6 {
			continue
		}

		// Create REDIRECT rule for UDP
		udpRule := []string{
			"-p", "udp",
			"-d", addr.IP.String(),
			"--dport", strconv.Itoa(sourcePort),
			"-j", "REDIRECT",
			"--to-port", strconv.Itoa(int(m.targetPort)),
		}
		if err := ipt.AppendUnique("nat", chainName, udpRule...); err != nil {
			return fmt.Errorf("failed to add UDP rule: %w", err)
		}

		// Create REDIRECT rule for TCP
		tcpRule := []string{
			"-p", "tcp",
			"-d", addr.IP.String(),
			"--dport", strconv.Itoa(sourcePort),
			"-j", "REDIRECT",
			"--to-port", strconv.Itoa(int(m.targetPort)),
		}
		if err := ipt.AppendUnique("nat", chainName, tcpRule...); err != nil {
			return fmt.Errorf("failed to add TCP rule: %w", err)
		}
	}

	// Link chain to PREROUTING
	if err := ipt.InsertUnique("nat", "PREROUTING", 1, "-j", chainName); err != nil {
		return fmt.Errorf("failed to link chain: %w", err)
	}

	return nil
}

// disable removes all iptables rules and chain.
func (m *IPTablesManager) disable() error {
	var errs []error

	if m.ipt4 != nil {
		if err := m.deleteChainAndRules(m.ipt4); err != nil {
			errs = append(errs, fmt.Errorf("IPv4: %w", err))
		}
	}

	if m.ipt6 != nil {
		if err := m.deleteChainAndRules(m.ipt6); err != nil {
			errs = append(errs, fmt.Errorf("IPv6: %w", err))
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("errors during cleanup: %v", errs)
	}

	return nil
}

// deleteChainAndRules removes the chain and its rules.
func (m *IPTablesManager) deleteChainAndRules(ipt *iptables.IPTables) error {
	// Unlink from PREROUTING
	if err := ipt.DeleteIfExists("nat", "PREROUTING", "-j", chainName); err != nil {
		log.Debugf("Failed to unlink chain: %v", err)
	}

	// Clear chain
	if err := ipt.ClearChain("nat", chainName); err != nil {
		log.Debugf("Failed to clear chain: %v", err)
	}

	// Delete chain
	if err := ipt.DeleteChain("nat", chainName); err != nil {
		log.Debugf("Failed to delete chain: %v", err)
	}

	return nil
}

// Refresh refreshes the iptables rules (e.g., after interface changes).
func (m *IPTablesManager) Refresh() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if !m.enabled {
		return nil
	}

	// Get current addresses
	addresses, err := getLocalAddresses()
	if err != nil {
		return fmt.Errorf("failed to get local addresses: %w", err)
	}

	// Check if addresses changed
	if addressesEqual(m.addresses, addresses) {
		return nil
	}

	log.Debugf("Local addresses changed, refreshing iptables rules")
	m.addresses = addresses

	// Recreate rules
	m.disable()
	return m.createRules()
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

// addressesEqual checks if two address slices are equal.
func addressesEqual(a, b []netlink.Addr) bool {
	if len(a) != len(b) {
		return false
	}

	aMap := make(map[string]bool)
	for _, addr := range a {
		aMap[addr.IP.String()] = true
	}

	for _, addr := range b {
		if !aMap[addr.IP.String()] {
			return false
		}
	}

	return true
}
