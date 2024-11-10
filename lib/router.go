package lib

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

const DNSName string = `^([a-zA-Z0-9_]{1}[a-zA-Z0-9_-]{0,62}){1}(\.[a-zA-Z0-9_]{1}[a-zA-Z0-9_-]{0,62})*[\._]?$`

var rxDNSName = regexp.MustCompile(DNSName)

func ApplyLists(config *Config) error {
	listsDir := filepath.Clean(config.General.ListsOutputDir)
	dnsmasqDir := filepath.Clean(config.General.DnsmasqConfDir)

	// Create required directories
	for _, dir := range []string{listsDir, dnsmasqDir} {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return fmt.Errorf("failed to create directory %s: %v", dir, err)
		}
	}

	// Remove old dnsmasq config files
	entries, err := os.ReadDir(dnsmasqDir)
	if err != nil {
		return fmt.Errorf("failed to read dnsmasq directory: %v", err)
	}

	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf") {
			path := filepath.Join(dnsmasqDir, entry.Name())
			log.Printf("Removing old dnsmasq config file: %s", path)
			if err := os.Remove(path); err != nil {
				return fmt.Errorf("failed to remove old config file: %v", err)
			}
		}
	}

	ipsetManager := &IpsetManager{}

	// Process lists
	for _, list := range config.List {
		var ipv4Networks []string
		var domains []string

		// Read and process list file
		content, err := os.ReadFile(filepath.Join(listsDir, fmt.Sprintf("%s.lst", list.Name)))
		if err != nil {
			return fmt.Errorf("failed to read list file: %v", err)
		}

		scanner := bufio.NewScanner(strings.NewReader(string(content)))
		for scanner.Scan() {
			line := strings.TrimSpace(scanner.Text())
			if line == "" || strings.HasPrefix(line, "#") {
				continue
			}

			if isDNSName(line) {
				domains = append(domains, line)
			} else if isIPv4(line) || IsCIDR(line) {
				ipv4Networks = append(ipv4Networks, line)
			}
		}

		if len(ipv4Networks) > 0 {
			// Summarize networks if requested
			var oldIpNetworksLen = len(ipv4Networks)
			if config.General.Summarize {
				ipv4Networks = NetworkSummarizer{}.SummarizeIPv4(ipv4Networks)
			}

			// Apply networks to ipsets
			if config.General.Summarize {
				log.Printf("Importing list \"%s\" addresses to ipset %s (%d -> %d items)...", list.Name, list.Ipset, oldIpNetworksLen, len(ipv4Networks))
			} else {
				log.Printf("Importing list \"%s\" addresses to ipset %s (%d items)...", list.Name, list.Ipset, oldIpNetworksLen)
			}
			if err := ipsetManager.AddToIpset(config.General.IpsetPath, list.Ipset, ipv4Networks); err != nil {
				return err
			}
		}

		// Write dnsmasq configuration
		if len(domains) > 0 {
			log.Printf("Creating dnsmasq conf for list \"%s\"...", list.Name)
			dnsmasqConf := filepath.Join(dnsmasqDir, fmt.Sprintf("%s.keenetic-pbr.conf", list.Name))
			f, err := os.Create(dnsmasqConf)
			if err != nil {
				return fmt.Errorf("failed to create dnsmasq config file: %v", err)
			}
			defer f.Close()

			writer := bufio.NewWriter(f)
			for _, domain := range domains {
				fmt.Fprintf(writer, "ipset=/%s/%s\n", domain, list.Ipset)
			}
			if err := writer.Flush(); err != nil {
				return fmt.Errorf("failed to write dnsmasq config: %v", err)
			}
		}
	}

	log.Print("Configuration applied successfully")
	return nil
}

// isDNSName will validate the given string as a DNS name
func isDNSName(str string) bool {
	if str == "" || len(strings.Replace(str, ".", "", -1)) > 255 {
		// constraints already violated
		return false
	}
	return !isIP(str) && rxDNSName.MatchString(str)
}

func isIP(str string) bool {
	return net.ParseIP(str) != nil
}

// isIPv4 checks if the string is an IP version 4.
func isIPv4(str string) bool {
	ip := net.ParseIP(str)
	return ip != nil && strings.Contains(str, ".")
}

// isIPv6 checks if the string is an IP version 6.
func isIPv6(str string) bool {
	ip := net.ParseIP(str)
	return ip != nil && strings.Contains(str, ":")
}

// IsCIDR checks if the string is an valid CIDR notiation (IPV4 & IPV6)
func IsCIDR(str string) bool {
	_, _, err := net.ParseCIDR(str)
	return err == nil
}
