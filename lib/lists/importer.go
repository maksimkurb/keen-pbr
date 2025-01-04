package lists

import (
	"bufio"
	"errors"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

func ApplyLists(cfg *config.Config, skipDnsmasq bool, skipIpset bool) error {
	listsDir := filepath.Clean(cfg.General.ListsOutputDir)
	dnsmasqDir := filepath.Clean(cfg.General.DnsmasqListsDir)

	if !skipDnsmasq {
		// Create required directories
		for _, dir := range []string{listsDir, dnsmasqDir} {
			if err := os.MkdirAll(dir, 0755); err != nil {
				return fmt.Errorf("failed to create directory %s: %v", dir, err)
			}
		}

		// Remove old dnsmasq cfg files
		entries, err := os.ReadDir(dnsmasqDir)
		if err != nil {
			return fmt.Errorf("failed to read dnsmasq directory: %v", err)
		}

		for _, entry := range entries {
			if strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf") {
				path := filepath.Join(dnsmasqDir, entry.Name())
				log.Infof("Removing old dnsmasq cfg file '%s'", path)
				if err := os.Remove(path); err != nil {
					return fmt.Errorf("failed to remove old cfg file: %v", err)
				}
			}
		}
	}

	for _, ipset := range cfg.Ipset {
		var ipv4Networks []string = make([]string, 0)
		var ipv6Networks []string = make([]string, 0)
		var domains []string = make([]string, 0)

		log.Infof("Processing ipset \"%s\": ", ipset.IpsetName)

		// Process lists
		for _, list := range ipset.List {
			if list.URL != "" || list.File != "" {
				listPath := getListPath(listsDir, ipset, list)

				listFile, err := os.Open(*listPath)
				if err != nil {
					log.Fatalf("Failed to read list file '%s': %v", *listPath, err)
				}
				defer listFile.Close()

				scanner := bufio.NewScanner(listFile)

				for scanner.Scan() {
					appendHost(scanner.Text(), &domains, &ipv4Networks, &ipv6Networks)
				}
			} else {
				for _, host := range list.Hosts {
					appendHost(host, &domains, &ipv4Networks, &ipv6Networks)
				}
			}
		}

		fmt.Printf("%d domains, %d ipv4 networks, %d ipv6 networks\n", len(domains), len(ipv4Networks), len(ipv6Networks))

		err := networking.CreateIpset(ipset)
		if err != nil {
			log.Warnf("Could not create ipset '%s': %v", ipset.IpsetName, err)
		}

		// Filling ipv4 ipset
		if !skipIpset && ipset.IpVersion != config.Ipv6 && len(ipv4Networks) > 0 {
			// Summarize networks if requested
			var ipsLen = len(ipv4Networks)
			if cfg.General.Summarize {
				ipv4Networks = networking.SummarizeIPv4(ipv4Networks)
			}

			// Apply networks to ipsets
			if cfg.General.Summarize {
				log.Infof("Filling ipset '%s' (IPv4) (%d items, %d after summarization)...", ipset.IpsetName, ipsLen, len(ipv4Networks))
			} else {
				log.Infof("Filling ipset '%s' (IPv4) (%d items)...", ipset.IpsetName, ipsLen)
			}
			if err := networking.AddToIpset(ipset, ipv4Networks); err != nil {
				log.Infof("Could not fill ipset (IPv4) '%s': %v", ipset.IpsetName, err)
			}
		}

		if !skipIpset && ipset.IpVersion == config.Ipv6 && len(ipv6Networks) > 0 {
			// Apply networks to ipsets
			log.Infof("Filling ipset '%s' (IPv6) (%d items)...", ipset.IpsetName, len(ipv6Networks))
			if err := networking.AddToIpset(ipset, ipv6Networks); err != nil {
				log.Warnf("Could not fill ipset (IPv6) '%s': %v", ipset.IpsetName, err)
			}
		}

		// Write dnsmasq configuration
		if !skipDnsmasq && len(domains) > 0 {
			dnsmasqConf := filepath.Join(dnsmasqDir, fmt.Sprintf("%s.keenetic-pbr.conf", ipset.IpsetName))
			log.Infof("Generating dnsmasq configuration for ipset '%s': %s", ipset.IpsetName, dnsmasqConf)
			f, err := os.Create(dnsmasqConf)
			if err != nil {
				return fmt.Errorf("failed to create dnsmasq cfg file: %v", err)
			}
			defer f.Close()

			domains = removeDuplicates(domains)

			writer := bufio.NewWriter(f)
			for _, domain := range domains {
				fmt.Fprintf(writer, "ipset=/%s/%s\n", domain, ipset.IpsetName)
			}
			if err := writer.Flush(); err != nil {
				return fmt.Errorf("failed to write dnsmasq cfg: %v", err)
			}
		}
	}

	log.Infof("Configuration applied")
	return nil
}

func appendHost(host string, domainsPtr *[]string, ipv4NetworksPtr *[]string, ipv6NetworksPtr *[]string) {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return
	}

	if utils.IsDNSName(line) {
		domains := *domainsPtr
		domains = append(domains, line)
		*domainsPtr = domains
	} else if utils.IsIP(line) || utils.IsCIDR(line) {
		// ipv4 contain dots, ipv6 contain colons
		if strings.Contains(line, ".") {
			ipv4Networks := *ipv4NetworksPtr
			ipv4Networks = append(ipv4Networks, line)
			*ipv4NetworksPtr = ipv4Networks
		} else {
			ipv6Networks := *ipv6NetworksPtr
			ipv6Networks = append(ipv6Networks, line)
			*ipv6NetworksPtr = ipv6Networks
		}
	} else {
		log.Warnf("Could not parse host, skipping: %s", host)
	}
}

func getListPath(listsDir string, ipset *config.IpsetConfig, list *config.ListSource) *string {
	var path = ""
	if list.URL != "" {
		path = filepath.Join(listsDir, fmt.Sprintf("%s-%s.lst", ipset.IpsetName, list.ListName))
	} else if list.File != "" {
		path = list.File
	}

	if path == "" {
		return nil
	}

	var err error
	path, err = filepath.Abs(path)
	if err != nil {
		log.Fatalf("Failed to resolve absolute path to file '%s': %v", path, err)
	}

	if _, err := os.Stat(path); errors.Is(err, os.ErrNotExist) {
		if list.URL != "" {
			log.Fatalf("Could not open list file '%s', please run 'keenetic-pbr download' first", path)
		} else {
			log.Fatalf("Could not open list file '%s'", path)
		}
	}

	return &path
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
