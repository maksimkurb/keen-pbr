package lib

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const DNSName string = `^([a-zA-Z0-9_]{1}[a-zA-Z0-9_-]{0,62}){1}(\.[a-zA-Z0-9_]{1}[a-zA-Z0-9_-]{0,62})*[\._]?$`

var rxDNSName = regexp.MustCompile(DNSName)

func ApplyLists(config *Config) error {
	listsDir := filepath.Clean(config.General.ListsOutputDir)
	dnsmasqDir := filepath.Clean(config.General.DnsmasqListsDir)

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
			log.Printf("Removing old dnsmasq config file '%s'", path)
			if err := os.Remove(path); err != nil {
				return fmt.Errorf("failed to remove old config file: %v", err)
			}
		}
	}

	for _, ipset := range config.Ipset {
		var ipv4Networks []string
		var ipv6Networks []string
		var domains []string

		// Process lists
		for _, list := range ipset.List {
			// Read and process list file
			content, err := os.ReadFile(filepath.Join(listsDir, fmt.Sprintf("%s-%s.lst", ipset.IpsetName, list.ListName)))
			if err != nil {
				log.Printf("failed to read list file %s-%s: %v\n\nplease, run \"keenetic-pbr download\"", err)
				continue
			}

			scanner := bufio.NewScanner(strings.NewReader(string(content)))
			for scanner.Scan() {
				line := strings.TrimSpace(scanner.Text())
				if line == "" || strings.HasPrefix(line, "#") {
					continue
				}

				if isDNSName(line) {
					domains = append(domains, line)
				} else if isIP(line) || isCIDR(line) {
					// ipv4 contain dots, ipv6 contain colons
					if strings.Contains(line, ".") {
						ipv4Networks = append(ipv4Networks, line)
					} else {
						ipv6Networks = append(ipv6Networks, line)
					}
				}
			}
		}

		err := CreateIpset(config.General.IpsetPath, ipset)
		if err != nil {
			log.Printf("Could not create ipset '%s': %v", ipset.IpsetName, err)
		}

		// Filling ipv4 ipset
		if ipset.IpVersion != 6 && len(ipv4Networks) > 0 {
			// Summarize networks if requested
			var ipsLen = len(ipv4Networks)
			if config.General.Summarize {
				ipv4Networks = SummarizeIPv4(ipv4Networks)
			}

			// Apply networks to ipsets
			if config.General.Summarize {
				log.Printf("Filling ipset '%s' (IPv4) (%d items, %d after summarization)...", ipset.IpsetName, ipsLen, len(ipv4Networks))
			} else {
				log.Printf("Filling ipset '%s' (IPv4) (%d items)...", ipset.IpsetName, ipsLen)
			}
			if err := AddToIpset(config.General.IpsetPath, ipset, ipv4Networks); err != nil {
				log.Printf("Could not fill ipset (IPv4) '%s': %v", ipset.IpsetName, err)
			}
		}

		if ipset.IpVersion == 6 && len(ipv6Networks) > 0 {
			// Apply networks to ipsets
			log.Printf("Filling ipset '%s' (IPv6) (%d items)...", ipset.IpsetName, len(ipv6Networks))
			if err := AddToIpset(config.General.IpsetPath, ipset, ipv6Networks); err != nil {
				log.Printf("Could not fill ipset (IPv6) '%s': %v", ipset.IpsetName, err)
			}
		}

		// Write dnsmasq configuration
		if len(domains) > 0 {
			dnsmasqConf := filepath.Join(dnsmasqDir, fmt.Sprintf("%s.keenetic-pbr.conf", ipset.IpsetName))
			log.Printf("Creating dnsmasq configuration for ipset '%s': %s", ipset.IpsetName, dnsmasqConf)
			f, err := os.Create(dnsmasqConf)
			if err != nil {
				return fmt.Errorf("failed to create dnsmasq config file: %v", err)
			}
			defer f.Close()

			domains = removeDuplicates(domains)

			writer := bufio.NewWriter(f)
			for _, domain := range domains {
				fmt.Fprintf(writer, "ipset=/%s/%s\n", domain, ipset.IpsetName)
			}
			if err := writer.Flush(); err != nil {
				return fmt.Errorf("failed to write dnsmasq config: %v", err)
			}
		}
	}

	log.Print("Configuration applied successfully")
	return nil
}

func removeDuplicates(strings []string) []string {
	sort.Strings(strings)
	// Remove duplicates
	if len(strings) > 0 {
		j := 1
		for i := 1; i < len(strings); i++ {
			if strings[i] != strings[i-1] {
				strings[j] = strings[i]
				j++
			}
		}
		strings = strings[:j]
	}
	return strings
}
