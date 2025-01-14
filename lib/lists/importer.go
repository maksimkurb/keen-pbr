package lists

import (
	"bufio"
	"errors"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/hashing"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
	"net/netip"
	"os"
	"path/filepath"
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
			if strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf") || strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf.md5") {
				shouldRemove := true
				for _, ipset := range cfg.Ipset {
					if strings.HasPrefix(entry.Name(), ipset.IpsetName+".keenetic-pbr.conf") {
						shouldRemove = false
						break
					}
				}

				if shouldRemove {
					path := filepath.Join(dnsmasqDir, entry.Name())
					log.Infof("Removing old dnsmasq cfg file '%s'", path)
					if err := os.Remove(path); err != nil {
						return fmt.Errorf("failed to remove old cfg file: %v", err)
					}
				}
			}
		}
	}

	for _, ipset := range cfg.Ipset {
		var ipv4Networks = make([]netip.Prefix, 0)
		var ipv6Networks = make([]netip.Prefix, 0)
		var domains = hashing.NewChecksumStringSet()

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
					appendHost(scanner.Text(), domains, &ipv4Networks, &ipv6Networks)
				}
			} else {
				for _, host := range list.Hosts {
					appendHost(host, domains, &ipv4Networks, &ipv6Networks)
				}
			}
		}

		log.Infof("Lists processing finished: %d domains, %d ipv4 networks, %d ipv6 networks", domains.Size(), len(ipv4Networks), len(ipv6Networks))

		err := networking.CreateIpset(ipset)
		if err != nil {
			log.Warnf("Could not create ipset '%s': %v", ipset.IpsetName, err)
		}

		if !skipIpset {
			if len(ipv4Networks) > 0 {
				if ipset.IpVersion == config.Ipv4 {
					fillIpset(ipset, ipv4Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IpsetName, 4, len(ipv4Networks), 6)
				}
			}

			if len(ipv6Networks) > 0 {
				if ipset.IpVersion == config.Ipv6 {
					fillIpset(ipset, ipv6Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IpsetName, 6, len(ipv6Networks), 4)
				}
			}
		}

		// Write dnsmasq configuration
		if !skipDnsmasq && domains.Size() > 0 {
			err2 := writeDnsmasqConfig(dnsmasqDir, ipset, domains)
			if err2 != nil {
				return err2
			}
		}
	}

	log.Infof("Configuration applied")
	return nil
}

func writeDnsmasqConfig(dnsmasqDir string, ipset *config.IpsetConfig, domains *hashing.ChecksumStringSetProxy) error {
	startTime := time.Now().UnixMilli()

	dnsmasqConfPath := filepath.Join(dnsmasqDir, fmt.Sprintf("%s.keenetic-pbr.conf", ipset.IpsetName))

	if changed, err := IsFileChanged(domains, dnsmasqConfPath); err != nil {
		return fmt.Errorf("failed to calculate dnsmasq cfg checksum: %v", err)
	} else if !changed {
		log.Infof("dnsmasq configuration for ipset '%s' is up-to-date, skipping it's generation", ipset.IpsetName)
		return nil
	}
	log.Infof("Generating dnsmasq configuration for ipset '%s': %s", ipset.IpsetName, dnsmasqConfPath)
	f, err := os.Create(dnsmasqConfPath)
	if err != nil {
		return fmt.Errorf("failed to create dnsmasq cfg file: %v", err)
	}
	defer f.Close()

	writer := bufio.NewWriter(f)
	for domain, _ := range domains.Map() {
		fmt.Fprintf(writer, "ipset=/%s/%s\n", domain, ipset.IpsetName)
	}
	if err := writer.Flush(); err != nil {
		return fmt.Errorf("failed to write dnsmasq cfg: %v", err)
	}
	if err := WriteChecksum(domains, dnsmasqConfPath); err != nil {
		return fmt.Errorf("failed to write dnsmasq cfg checksum: %v", err)
	}

	log.Infof("Writing dnsmasq configutaion took %dms", time.Now().UnixMilli()-startTime)
	log.Warnf("Please restart dnsmasq service for changes to take effect!")

	return nil
}

func fillIpset(ipset *config.IpsetConfig, networks []netip.Prefix) {
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

func appendHost(host string, domainsPtr *hashing.ChecksumStringSetProxy, ipv4NetworksPtr *[]netip.Prefix, ipv6NetworksPtr *[]netip.Prefix) {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return
	}

	if utils.IsDNSName(line) {
		err := domainsPtr.Put(line)
		if err != nil {
			log.Warnf("Could not add host '%s' to list: %v", host, err)
			return
		}
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
