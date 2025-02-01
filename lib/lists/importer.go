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
	"runtime"
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
				for _, ipset := range cfg.IPSets {
					if strings.HasPrefix(entry.Name(), ipset.IPSetName+".keenetic-pbr.conf") {
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

	var domainStore = CreateDomainStore(len(cfg.IPSets))

	for ipsetIndex, ipset := range cfg.IPSets {
		var ipv4Networks = make([]netip.Prefix, 0)
		var ipv6Networks = make([]netip.Prefix, 0)

		log.Infof("Processing ipset \"%s\": ", ipset.IPSetName)

		// Process lists
		for _, list := range ipset.Lists {
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
					appendHost(scanner.Text(), ipsetIndex, domainStore, &ipv4Networks, &ipv6Networks)
				}
			} else {
				for _, host := range list.Hosts {
					appendHost(host, ipsetIndex, domainStore, &ipv4Networks, &ipv6Networks)
				}
			}
		}

		log.Infof("Lists processing finished: %d domains, %d ipv4 networks, %d ipv6 networks", domainStore.Count(), len(ipv4Networks), len(ipv6Networks))

		err := networking.CreateIpset(ipset)
		if err != nil {
			log.Warnf("Could not create ipset '%s': %v", ipset.IPSetName, err)
		}

		if !skipIpset {
			if len(ipv4Networks) > 0 {
				if ipset.IPVersion == config.Ipv4 {
					fillIpset(ipset, ipv4Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IPSetName, 4, len(ipv4Networks), 6)
				}
			}

			if len(ipv6Networks) > 0 {
				if ipset.IPVersion == config.Ipv6 {
					fillIpset(ipset, ipv6Networks)
				} else {
					log.Warnf("Lists for ipset '%s' (IPv%d) contains %d of IPv%d networks, skipping them",
						ipset.IPSetName, 6, len(ipv6Networks), 4)
				}
			}
		}
	}

	// Write dnsmasq configuration
	if !skipDnsmasq && domainStore.Count() > 0 {
		err2 := writeDnsmasqConfig(listsDir, dnsmasqDir, cfg, domainStore)
		if err2 != nil {
			return err2
		}
	}

	rtm := runtime.MemStats{}
	runtime.ReadMemStats(&rtm)
	fmt.Println("Alloc:", rtm.HeapAlloc)

	log.Infof("Configuration applied")
	return nil
}

func writeDnsmasqConfig(listsDir, dnsmasqDir string, config *config.Config, domains *DomainStore) error {
	startTime := time.Now().UnixMilli()

	ipsetCount := len(config.IPSets)
	files := make(map[rune]*os.File)
	fileBuffers := make(map[rune]*bufio.Writer)
	defer func() {
		for idx, f := range files {
			if err := fileBuffers[idx].Flush(); err != nil {
				log.Errorf("Failed to flush file: %v", err)
			}
			if err := f.Close(); err != nil {
				log.Errorf("Failed to close file: %v", err)
			}
		}
	}()

	log.Infof("Generating dnsmasq configuration...")

	writeHost := func(domain string) {
		if !utils.IsDNSName(domain) {
			return
		}

		sanitizedDomain := sanitizeDomain(domain)

		// there is no problem with collisions for single ipset
		if ipsetCount > 1 {
			if collision := domains.GetCollisionDomain(sanitizedDomain); collision != "" {
				log.Warnf("Found collision: \"%s\" and \"%s\" have the same CRC32-hash. Routing for both of these domains will be undetermined. To fix this, please remove one of these domains", domain, collision)
			}
		}

		associations, hash := domains.GetAssociatedIPSetIndexesForDomain(sanitizedDomain)
		if associations == nil {
			return
		}

		fileRune := rune(domain[0])
		if _, ok := files[fileRune]; !ok {
			path := filepath.Join(dnsmasqDir, fmt.Sprintf("%c.keenetic-pbr.conf", fileRune))
			f, err := os.Create(path)
			if err != nil {
				log.Fatalf("Failed to create dnsmasq cfg file '%s': %v", path, err)
				return
			}
			files[fileRune] = f
			fileBuffers[fileRune] = bufio.NewWriter(f)
		}
		writer := fileBuffers[fileRune]

		if _, err := fmt.Fprintf(writer, "ipset=/%s/", domain); err != nil {
			log.Errorf("Failed to write to dnsmasq cfg file: %v", err)
			return
		}

		isFirstIPSet := true
		for i := 0; i < ipsetCount; i++ {
			if !associations.Has(i) {
				continue
			}

			if !isFirstIPSet {
				if _, err := writer.WriteRune(','); err != nil {
					log.Errorf("Failed to write to dnsmasq cfg file: %v", err)
					return
				}
			}

			isFirstIPSet = false

			if _, err := writer.WriteString(config.IPSets[i].IPSetName); err != nil {
				log.Errorf("Failed to write to dnsmasq cfg file: %v", err)
				return
			}
		}

		if _, err := writer.WriteRune('\n'); err != nil {
			log.Errorf("Failed to write to dnsmasq cfg file: %v", err)
			return
		}

		domains.Forget(hash)
	}

	for _, ipset := range config.IPSets {
		for _, list := range ipset.Lists {
			if list.URL != "" || list.File != "" {
				listPath := getListPath(listsDir, ipset, list)

				listFile, err := os.Open(*listPath)
				if err != nil {
					log.Fatalf("Failed to read list file '%s': %v", *listPath, err)
				}
				defer listFile.Close()

				scanner := bufio.NewScanner(listFile)

				for scanner.Scan() {
					writeHost(scanner.Text())
				}
			} else {
				for _, host := range list.Hosts {
					writeHost(host)
				}
			}
		}
	}

	log.Infof("Writing dnsmasq configutaion took %dms", time.Now().UnixMilli()-startTime)
	log.Warnf("Please restart dnsmasq service for changes to take effect!")

	return nil
}

func fillIpset(ipset *config.IPSetConfig, networks []netip.Prefix) {
	startTime := time.Now().UnixMilli()
	// Apply networks to ipsets
	log.Infof("Filling ipset '%s' (IPv%d) (%d networks)...",
		ipset.IPSetName, ipset.IPVersion, len(networks))
	if err := networking.AddToIpset(ipset, networks); err != nil {
		log.Infof("Could not fill ipset '%s' (IPv%d): %v", ipset.IPSetName, ipset.IPVersion, err)
	}
	log.Infof("Filling ipset '%s' (IPv%d) took %dms",
		ipset.IPSetName, ipset.IPVersion, time.Now().UnixMilli()-startTime)
}

func appendHost(host string, ipsetIndex int, domainStore *DomainStore, ipv4NetworksPtr *[]netip.Prefix, ipv6NetworksPtr *[]netip.Prefix) {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return
	}

	if utils.IsDNSName(line) {
		domainStore.AssociateDomainWithIPSet(sanitizeDomain(line), ipsetIndex)
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

func getListPath(listsDir string, ipset *config.IPSetConfig, list *config.ListSource) *string {
	var path = ""
	if list.URL != "" {
		path = filepath.Join(listsDir, fmt.Sprintf("%s-%s.lst", ipset.IPSetName, list.ListName))
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
