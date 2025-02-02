package lists

import (
	"bufio"
	"fmt"
	"net/netip"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
)

// ApplyLists processes the configuration and applies the lists to the appropriate sets.
func ApplyLists(cfg *config.Config, skipDnsmasq, skipIpset bool) error {
	if err := prepareDirectories(cfg.GetAbsDownloadedListsDir(), cfg.GetAbsDnsmasqDir(), skipDnsmasq); err != nil {
		return err
	}

	if !skipDnsmasq {
		if err := cleanupOldDnsmasqConfigs(cfg.GetAbsDnsmasqDir(), cfg.IPSets); err != nil {
			return err
		}
	}

	domainStore := CreateDomainStore(len(cfg.IPSets))
	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		ipCount := 0

		log.Infof("Processing ipset \"%s\": ", ipsetCfg.IPSetName)

		ipset := networking.BuildIPSet(ipsetIndex, ipsetCfg.IPSetName, ipsetCfg.IPVersion)
		if err := ipset.CreateIfNotExists(); err != nil {
			if !skipIpset {
				return err
			} else {
				log.Warnf("ipset '%s' is not created: %v", ipsetCfg.IPSetName, err)
			}
		}

		if ipsetWriter, err := ipset.OpenWriter(); err != nil {
			return err
		} else {
			defer ipsetWriter.Close()

			for _, listName := range ipsetCfg.Lists {
				if err := processList(cfg, listName, ipsetWriter, domainStore, &ipCount); err != nil {
					return err
				}
			}
		}

		log.Infof("ipset processing finished: %d domains, %d IPs/networks", domainStore.Count(), ipCount)
	}

	if !skipDnsmasq && domainStore.Count() > 0 {
		if err := writeDnsmasqConfig(cfg, domainStore); err != nil {
			return err
		}
	}

	log.Infof("Configuration applied")
	return nil
}

// prepareDirectories ensures that the required directories exist.
func prepareDirectories(listsDir, dnsmasqDir string, skipDnsmasq bool) error {
	if skipDnsmasq {
		return nil
	}

	for _, dir := range []string{listsDir, dnsmasqDir} {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return fmt.Errorf("failed to create directory %s: %v", dir, err)
		}
	}
	return nil
}

// cleanupOldDnsmasqConfigs removes old dnsmasq configuration files.
func cleanupOldDnsmasqConfigs(dnsmasqDir string, ipsets []*config.IPSetConfig) error {
	entries, err := os.ReadDir(dnsmasqDir)
	if err != nil {
		return fmt.Errorf("failed to read dnsmasq directory: %v", err)
	}

	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf") || strings.HasSuffix(entry.Name(), ".keenetic-pbr.conf.md5") {
			shouldRemove := true
			for _, ipset := range ipsets {
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
	return nil
}

// processList processes a single list within an IP set.
func processList(cfg *config.Config, listName string, ipsetWriter *networking.IPSetWriter, domainStore *DomainStore, ipCount *int) error {
	list, err := getListByName(cfg, listName)
	if err != nil {
		return err
	}

	log.Infof("Reading list \"%s\" (type=%s)...", list.ListName, list.Type())

	if err := iterateOverList(list, cfg, func(host string) error {
		return appendHost(host, ipsetWriter, domainStore, ipCount)
	}); err != nil {
		return err
	}

	return nil
}

func getListByName(cfg *config.Config, listName string) (*config.ListSource, error) {
	for _, l := range cfg.Lists {
		if l.ListName == listName {
			return l, nil
		}
	}
	return nil, fmt.Errorf("list \"%s\" not found", listName)
}

// writeDnsmasqConfig writes the dnsmasq configuration.
func writeDnsmasqConfig(cfg *config.Config, domains *DomainStore) error {
	startTime := time.Now().UnixMilli()

	dnsmasqConfigWriter := NewDnsmasqConfigWriter(cfg)
	defer dnsmasqConfigWriter.Close()

	log.Infof("Generating dnsmasq configuration...")

	for _, ipset := range cfg.IPSets {
		for _, listName := range ipset.Lists {
			list, err := getListByName(cfg, listName)
			if err != nil {
				return err
			}

			if err := iterateOverList(list, cfg, func(host string) error {
				return writeHostToDnsmasq(host, dnsmasqConfigWriter, domains)
			}); err != nil {
				return err
			}
		}
	}

	log.Infof("Writing dnsmasq configuration took %dms", time.Now().UnixMilli()-startTime)
	log.Warnf("Please restart dnsmasq service for changes to take effect!")

	return nil
}

// writeHostToDnsmasq writes a single host to the dnsmasq configuration.
func writeHostToDnsmasq(domain string, dnsmasqConfigWriter *DnsmasqConfigWriter, domains *DomainStore) error {
	if !utils.IsDNSName(domain) {
		return nil
	}

	sanitizedDomain := sanitizeDomain(domain)

	if dnsmasqConfigWriter.GetIPSetCount() > 1 {
		if collision := domains.GetCollisionDomain(sanitizedDomain); collision != "" {
			log.Warnf("Found collision: \"%s\" and \"%s\" have the same CRC32-hash. "+
				"Routing for both of these domains will be undetermined. "+
				"To fix this, please remove one of these domains", domain, collision)
		}
	}

	associations, hash := domains.GetAssociatedIPSetIndexesForDomain(sanitizedDomain)
	if associations == nil {
		return nil
	}

	configID := getConfigID(domain)
	err := dnsmasqConfigWriter.WriteDomain(configID, sanitizedDomain, associations)
	if err != nil {
		return fmt.Errorf("failed to write domain to dnsmasq config: %v", err)
	}

	domains.Forget(hash)

	return nil
}

// appendHost appends a host to the appropriate networks or domain store.
func appendHost(host string, ipsetWriter *networking.IPSetWriter, domainStore *DomainStore, ipCount *int) error {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return nil
	}

	if utils.IsDNSName(line) {
		domainStore.AssociateDomainWithIPSet(sanitizeDomain(line), ipsetWriter.GetIPSet().Index)
	} else {
		if strings.LastIndex(line, "/") < 0 {
			line = line + "/32"
		}
		if netPrefix, err := netip.ParsePrefix(line); err == nil {
			if !netPrefix.IsValid() {
				log.Warnf("Could not parse host, skipping: %s", host)
				return nil
			}

			if (netPrefix.Addr().Is4() && ipsetWriter.GetIPSet().IpFamily == config.Ipv4) ||
				(netPrefix.Addr().Is6() && ipsetWriter.GetIPSet().IpFamily == config.Ipv6) {
				*ipCount++
				if err := ipsetWriter.Add(netPrefix); err != nil {
					return err
				}
			} else {
				log.Warnf("Could not parse host, skipping: %s", host)
			}
		} else {
			log.Warnf("Could not parse host, skipping: %s", host)
		}
	}

	return nil
}

func iterateOverList(list *config.ListSource, cfg *config.Config, iterateFn func(string) error) error {
	if list.URL != "" || list.File != "" {
		if listPath, err := list.GetAbsolutePathAndCheckExists(cfg); err != nil {
			return err
		} else {
			listFile, err := os.Open(listPath)
			if err != nil {
				return fmt.Errorf("failed to read list file '%s': %v", listPath, err)
			}
			defer listFile.Close()

			scanner := bufio.NewScanner(listFile)
			for scanner.Scan() {
				if err := iterateFn(scanner.Text()); err != nil {
					return err
				}
			}
			return nil
		}
	} else {
		for _, host := range list.Hosts {
			if err := iterateFn(host); err != nil {
				return err
			}
		}
		return nil
	}
}
