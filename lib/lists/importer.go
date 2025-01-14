package lists

import (
	"bufio"
	"errors"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
	"net/netip"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
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
		var ipv4Networks = make([]netip.Prefix, 0)
		var ipv6Networks = make([]netip.Prefix, 0)
		var domains = make([]string, 0)

		log.Infof("Processing ipset \"%s\": ", ipset.IpsetName)

		// Process lists
		for _, list := range ipset.List {
			log.Infof("Processing list \"%s\" (type=%s)...", list.ListName, list.Type())
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

		log.Infof("Lists processing finished: %d domains, %d ipv4 networks, %d ipv6 networks", len(domains), len(ipv4Networks), len(ipv6Networks))

		err := networking.CreateIpset(ipset)
		if err != nil {
			log.Warnf("Could not create ipset '%s': %v", ipset.IpsetName, err)
		}

		if !skipIpset {
			if len(ipv4Networks) > 0 {
				if ipset.IpVersion == config.Ipv4 {
					fillIpset(cfg, ipset, ipv4Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IpsetName, 4, len(ipv4Networks), 6)
				}
			}

			if len(ipv6Networks) > 0 {
				if ipset.IpVersion == config.Ipv6 {
					fillIpset(cfg, ipset, ipv6Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IpsetName, 6, len(ipv6Networks), 4)
				}
			}
		}

		// Write dnsmasq configuration
		if !skipDnsmasq && len(domains) > 0 {
			startTime := time.Now().UnixMilli()

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

			log.Infof("Writing dnsmasq configutaion took %dms", time.Now().UnixMilli()-startTime)
			log.Warnf("Please restart dnsmasq service for changes to take effect!")
		}
	}

	log.Infof("Configuration applied")
	return nil
}

func fillIpset(cfg *config.Config, ipset *config.IpsetConfig, networks []netip.Prefix) {
	startTime := time.Now().UnixMilli()
	// Apply networks to ipsets
	log.Infof("Filling ipset '%s' (IPv%d) (%d networks)...",
		ipset.IpsetName, ipset.IpVersion, len(networks))
	if err := networking.AddToIpset(ipset, networks); err != nil {
		log.Infof("Could not fill ipset '%s' (IPv%d): %v", ipset.IpsetName, ipset.IpVersion, err)
	}
	log.Infof("Filling ipset '%s' (IPv%d) took %dms",
		ipset.IpsetName, ipset.IpVersion, time.Now().UnixMilli()-startTime)
}

func appendHost(host string, domainsPtr *[]string, ipv4NetworksPtr *[]netip.Prefix, ipv6NetworksPtr *[]netip.Prefix) {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return
	}

	if utils.IsDNSName(line) {
		domains := *domainsPtr
		domains = append(domains, line)
		*domainsPtr = domains
	} else {
		if strings.LastIndex(line, "/") < 0 {
			line = line + "/32"
		}
		if netPrefix, err := netip.ParsePrefix(line); err == nil {
			if !netPrefix.IsValid() {
				log.Warnf("Could not parse host, skipping: %s", host)
				return
			}

			// ipv4 contain dots, ipv6 contain colons
			if netPrefix.Addr().Is4() {
				ipv4Networks := *ipv4NetworksPtr
				ipv4Networks = append(ipv4Networks, netPrefix)
				*ipv4NetworksPtr = ipv4Networks
			} else if netPrefix.Addr().Is6() {
				ipv6Networks := *ipv6NetworksPtr
				ipv6Networks = append(ipv6Networks, netPrefix)
				*ipv6NetworksPtr = ipv6Networks
			} else {
				log.Warnf("Could not parse host, skipping: %s", host)
			}
		} else {
			log.Warnf("Could not parse host, skipping: %s", host)
		}
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
