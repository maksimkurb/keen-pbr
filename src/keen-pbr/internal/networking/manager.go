package networking

import (
	"fmt"
	"os/exec"
	"strings"
)

const (
	// Packet mark and routing table ID (0x105 = 261 decimal)
	FWMARK_HEX = "0x105"
	FWMARK_DEC = "261"
	TABLE_ID   = "105"
	PRIORITY   = "105"

	// TPROXY target address
	TPROXY_ADDR = "127.0.0.1"
	TPROXY_PORT = "1602"

	// IPSet names
	IPSET_LOCAL_V4        = "keen_pbr_localv4"
	IPSET_PODKOP_SUBNETS  = "keen_pbr_podkop_subnets"
	IPSET_DISCORD_SUBNETS = "keen_pbr_discord_subnets"
)

// NetworkManager manages all networking components (ipsets, iptables)
type NetworkManager struct {
	ipsets     []*IPSet
	iptables   *IPTablesManager
	interfaces []string
}

// NewNetworkManager creates a new network manager
func NewNetworkManager(interfaces []string) *NetworkManager {
	// Default to br0 if no interfaces specified
	if len(interfaces) == 0 {
		interfaces = []string{"br0"}
	}
	return &NetworkManager{
		ipsets:     []*IPSet{},
		iptables:   NewIPTablesManager(),
		interfaces: interfaces,
	}
}

// loadTPROXYModule loads the TPROXY kernel module if not already loaded
func (m *NetworkManager) loadTPROXYModule() error {
	// Check if module is already loaded
	cmd := exec.Command("lsmod")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to check loaded modules: %w", err)
	}

	// Check if xt_TPROXY is already loaded
	if strings.Contains(string(output), "xt_TPROXY") {
		return nil // Module already loaded
	}

	// Get kernel version for module path
	kernelCmd := exec.Command("uname", "-r")
	kernelOutput, err := kernelCmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to get kernel version: %w", err)
	}
	kernelVersion := strings.TrimSpace(string(kernelOutput))

	// Construct module path
	modulePath := fmt.Sprintf("/lib/modules/%s/xt_TPROXY.ko", kernelVersion)

	// Load the module using insmod
	insmodCmd := exec.Command("insmod", modulePath)
	insmodOutput, err := insmodCmd.CombinedOutput()
	if err != nil {
		// Try modprobe as fallback
		modprobeCmd := exec.Command("modprobe", "xt_TPROXY")
		modprobeOutput, err := modprobeCmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("failed to load TPROXY module with insmod (%s) and modprobe (%s)",
				string(insmodOutput), string(modprobeOutput))
		}
	}

	return nil
}

// setupRoutingTable creates routing table for TPROXY
func (m *NetworkManager) setupRoutingTable() error {
	// Check if route already exists
	checkCmd := exec.Command("ip", "route", "list", "table", TABLE_ID)
	output, err := checkCmd.CombinedOutput()
	if err == nil && strings.Contains(string(output), "local default dev lo") {
		// Route already exists
		return nil
	}

	// Add route: ip route add local 0.0.0.0/0 dev lo table 105
	addCmd := exec.Command("ip", "route", "add", "local", "0.0.0.0/0", "dev", "lo", "table", TABLE_ID)
	if output, err := addCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add routing table: %w, output: %s", err, string(output))
	}

	return nil
}

// setupRoutingRule creates IP rule for marked packets
func (m *NetworkManager) setupRoutingRule() error {
	// Check if rule already exists
	checkCmd := exec.Command("ip", "rule", "list")
	output, err := checkCmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to list ip rules: %w", err)
	}

	// Check if rule exists: "from all fwmark 0x105 lookup 105"
	if strings.Contains(string(output), "fwmark "+FWMARK_HEX) && strings.Contains(string(output), "lookup "+TABLE_ID) {
		// Rule already exists
		return nil
	}

	// Add rule: ip -4 rule add fwmark 0x105 table 105 priority 105
	addCmd := exec.Command("ip", "-4", "rule", "add", "fwmark", FWMARK_HEX, "table", TABLE_ID, "priority", PRIORITY)
	if output, err := addCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add ip rule: %w, output: %s", err, string(output))
	}

	return nil
}

// teardownRoutingTable removes routing table
func (m *NetworkManager) teardownRoutingTable() error {
	// Check if route exists
	checkCmd := exec.Command("ip", "route", "list", "table", TABLE_ID)
	output, err := checkCmd.CombinedOutput()
	if err != nil || !strings.Contains(string(output), "local default dev lo") {
		// Route doesn't exist, nothing to do
		return nil
	}

	// Delete route: ip route del local 0.0.0.0/0 dev lo table 105
	delCmd := exec.Command("ip", "route", "del", "local", "0.0.0.0/0", "dev", "lo", "table", TABLE_ID)
	if output, err := delCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete routing table: %w, output: %s", err, string(output))
	}

	return nil
}

// teardownRoutingRule removes IP rule
func (m *NetworkManager) teardownRoutingRule() error {
	// Check if rule exists
	checkCmd := exec.Command("ip", "rule", "list")
	output, err := checkCmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to list ip rules: %w", err)
	}

	if !strings.Contains(string(output), "fwmark "+FWMARK_HEX) || !strings.Contains(string(output), "lookup "+TABLE_ID) {
		// Rule doesn't exist, nothing to do
		return nil
	}

	// Delete rule: ip -4 rule del fwmark 0x105 table 105
	delCmd := exec.Command("ip", "-4", "rule", "del", "fwmark", FWMARK_HEX, "table", TABLE_ID)
	if output, err := delCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete ip rule: %w, output: %s", err, string(output))
	}

	return nil
}

// Setup configures all ipsets, iptables rules, routing table and rules
func (m *NetworkManager) Setup() error {
	// Load TPROXY kernel module first
	if err := m.loadTPROXYModule(); err != nil {
		return fmt.Errorf("failed to load TPROXY module: %w", err)
	}

	// Setup routing table and rule for TPROXY
	if err := m.setupRoutingTable(); err != nil {
		return fmt.Errorf("failed to setup routing table: %w", err)
	}

	if err := m.setupRoutingRule(); err != nil {
		return fmt.Errorf("failed to setup routing rule: %w", err)
	}

	if err := m.setupIPSets(); err != nil {
		return fmt.Errorf("failed to setup ipsets: %w", err)
	}

	if err := m.setupIPTablesRules(); err != nil {
		return fmt.Errorf("failed to setup iptables rules: %w", err)
	}

	return nil
}

// setupIPSets creates and populates ipsets
func (m *NetworkManager) setupIPSets() error {
	// IPSet for local/private IPv4 addresses (RFC1918, loopback, etc.)
	localv4 := NewIPSet(IPSET_LOCAL_V4, "hash:net", "inet")
	localv4.AddElement("0.0.0.0/8")
	localv4.AddElement("10.0.0.0/8")
	localv4.AddElement("127.0.0.0/8")
	localv4.AddElement("169.254.0.0/16")
	localv4.AddElement("172.16.0.0/12")
	localv4.AddElement("192.0.0.0/24")
	localv4.AddElement("192.0.2.0/24")
	localv4.AddElement("192.88.99.0/24")
	localv4.AddElement("192.168.0.0/16")
	localv4.AddElement("198.51.100.0/24")
	localv4.AddElement("203.0.113.0/24")
	localv4.AddElement("224.0.0.0/3")

	// IPSet for podkop subnets (empty initially, can be populated dynamically)
	podkopSubnets := NewIPSet(IPSET_PODKOP_SUBNETS, "hash:net", "inet")

	// IPSet for Discord-related subnets
	discordSubnets := NewIPSet(IPSET_DISCORD_SUBNETS, "hash:net", "inet")
	discordSubnets.AddElement("5.200.14.128/25")
	discordSubnets.AddElement("34.0.0.0/14")
	discordSubnets.AddElement("35.192.0.0/11")
	discordSubnets.AddElement("66.22.192.0/18")
	discordSubnets.AddElement("138.128.136.0/21")
	discordSubnets.AddElement("162.158.0.0/15")
	discordSubnets.AddElement("172.64.0.0/13")

	m.ipsets = []*IPSet{localv4, podkopSubnets, discordSubnets}

	// Create and populate ipsets
	for _, ipset := range m.ipsets {
		if err := ipset.Create(); err != nil {
			return err
		}
		if err := ipset.AddElements(); err != nil {
			return err
		}
	}

	return nil
}

// setupIPTablesRules creates iptables rules based on nftables config
func (m *NetworkManager) setupIPTablesRules() error {
	// MANGLE PREROUTING rules
	// Create rules for each configured interface
	for _, iface := range m.interfaces {
		// Mark packets from interface to podkop subnets
		m.iptables.AddRule("mangle", "PREROUTING",
			"-i", iface,
			"-m", "set", "--match-set", IPSET_PODKOP_SUBNETS, "dst",
			"-p", "tcp",
			"-j", "MARK", "--set-mark", FWMARK_HEX)

		m.iptables.AddRule("mangle", "PREROUTING",
			"-i", iface,
			"-m", "set", "--match-set", IPSET_PODKOP_SUBNETS, "dst",
			"-p", "udp",
			"-j", "MARK", "--set-mark", FWMARK_HEX)

		// Mark packets to FakeIP range (198.18.0.0/15)
		m.iptables.AddRule("mangle", "PREROUTING",
			"-i", iface,
			"-d", "198.18.0.0/15",
			"-p", "tcp",
			"-j", "MARK", "--set-mark", FWMARK_HEX)

		m.iptables.AddRule("mangle", "PREROUTING",
			"-i", iface,
			"-d", "198.18.0.0/15",
			"-p", "udp",
			"-j", "MARK", "--set-mark", FWMARK_HEX)

		// Mark Discord UDP traffic (high ports)
		m.iptables.AddRule("mangle", "PREROUTING",
			"-i", iface,
			"-m", "set", "--match-set", IPSET_DISCORD_SUBNETS, "dst",
			"-p", "udp",
			"--dport", "50000:65535",
			"-j", "MARK", "--set-mark", FWMARK_HEX)
	}

	// MANGLE OUTPUT rules
	// Mark outgoing packets to podkop subnets (skip local IPs)
	m.iptables.AddRule("mangle", "OUTPUT",
		"-m", "set", "--match-set", IPSET_LOCAL_V4, "dst",
		"-j", "RETURN")

	m.iptables.AddRule("mangle", "OUTPUT",
		"-m", "set", "--match-set", IPSET_PODKOP_SUBNETS, "dst",
		"-p", "tcp",
		"-j", "MARK", "--set-mark", FWMARK_HEX)

	m.iptables.AddRule("mangle", "OUTPUT",
		"-m", "set", "--match-set", IPSET_PODKOP_SUBNETS, "dst",
		"-p", "udp",
		"-j", "MARK", "--set-mark", FWMARK_HEX)

	// Mark outgoing packets to FakeIP range (TCP only, as per nftables config)
	m.iptables.AddRule("mangle", "OUTPUT",
		"-d", "198.18.0.0/15",
		"-p", "tcp",
		"-j", "MARK", "--set-mark", FWMARK_HEX)

	// PREROUTING TPROXY rules
	// Redirect marked packets to tproxy
	m.iptables.AddRule("mangle", "PREROUTING",
		"-m", "mark", "--mark", FWMARK_HEX,
		"-p", "tcp",
		"-j", "TPROXY",
		"--on-ip", TPROXY_ADDR,
		"--on-port", TPROXY_PORT,
		"--tproxy-mark", FWMARK_HEX)

	m.iptables.AddRule("mangle", "PREROUTING",
		"-m", "mark", "--mark", FWMARK_HEX,
		"-p", "udp",
		"-j", "TPROXY",
		"--on-ip", TPROXY_ADDR,
		"--on-port", TPROXY_PORT,
		"--tproxy-mark", FWMARK_HEX)

	// Apply the rules
	return m.iptables.ApplyRules()
}

// Teardown removes all ipsets and iptables rules
func (m *NetworkManager) Teardown() error {
	// Remove iptables rules first
	if err := m.iptables.RemoveRules(); err != nil {
		return fmt.Errorf("failed to remove iptables rules: %w", err)
	}

	// Destroy ipsets
	for _, ipset := range m.ipsets {
		if err := ipset.Destroy(); err != nil {
			return fmt.Errorf("failed to destroy ipset: %w", err)
		}
	}

	// Remove routing rule
	if err := m.teardownRoutingRule(); err != nil {
		return fmt.Errorf("failed to teardown routing rule: %w", err)
	}

	// Remove routing table
	if err := m.teardownRoutingTable(); err != nil {
		return fmt.Errorf("failed to teardown routing table: %w", err)
	}

	return nil
}
