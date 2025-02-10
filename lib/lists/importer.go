package lists

import (
	"bufio"
	"fmt"
	"net/netip"
	"os"
	"strings"
	"time"

	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
)

// ImportListsToIPSets processes the configuration and applies the lists to the appropriate ipsets.
func ImportListsToIPSets(cfg *config.Config) error {
	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		ipCount := 0

		ipset := networking.BuildIPSet(ipsetCfg.IPSetName, ipsetCfg.IPVersion)

		if ipsetCfg.FlushBeforeApplying {
			if err := ipset.Flush(); err != nil {
				log.Errorf("Failed to flush ipset \"%s\": %v", ipsetCfg.IPSetName, err)
			} else {
				log.Infof("Flushed ipset \"%s\"", ipsetCfg.IPSetName)
			}
		}

		if err := ipset.CreateIfNotExists(); err != nil {
			return err
		}

		if ipsetWriter, err := ipset.OpenWriter(); err != nil {
			return err
		} else {
			defer utils.CloseOrPanic(ipsetWriter)

			for _, listName := range ipsetCfg.Lists {
				log.Infof("Importing list \"%s\" into ipset \"%s\"...", listName, ipsetCfg.IPSetName)

				if err := processList(cfg, ipsetIndex, listName, ipsetWriter, nil, &ipCount); err != nil {
					return err
				}
			}
		}

		log.Infof("ipset \"%s\" processing finished: %d IPs/networks loaded to ipset", ipsetCfg.IPSetName, ipCount)
	}

	log.Infof("All IPs/networks loaded to ipsets")
	return nil
}

// PrintDnsmasqConfig processes the configuration and prints the dnsmasq configuration.
func PrintDnsmasqConfig(cfg *config.Config) error {
	domainStore := CreateDomainStore(len(cfg.IPSets))
	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		for _, listName := range ipsetCfg.Lists {
			log.Infof("Parsing list \"%s\" for ipset \"%s\"...", listName, ipsetCfg.IPSetName)

			if err := processList(cfg, ipsetIndex, listName, nil, domainStore, nil); err != nil {
				return err
			}
		}
	}

	log.Infof("Parsed %d domains. Producing dnsmasq config into stdout...", domainStore.Count())

	if domainStore.Count() > 0 {
		if err := printDnsmasqConfig(cfg, domainStore); err != nil {
			return err
		}
	}

	return nil
}

// processList processes a single list within an IP set.
func processList(cfg *config.Config, ipsetIndex int, listName string, ipsetWriter *networking.IPSetWriter, domainStore *DomainStore, ipCount *int) error {
	list, err := getListByName(cfg, listName)
	if err != nil {
		return err
	}

	log.Infof("Reading list \"%s\" (type=%s)...", list.ListName, list.Type())

	if err := iterateOverList(list, cfg, func(host string) error {
		return appendHost(host, ipsetIndex, ipsetWriter, domainStore, ipCount)
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

// printDnsmasqConfig writes the dnsmasq configuration to stdout.
func printDnsmasqConfig(cfg *config.Config, domains *DomainStore) error {
	startTime := time.Now().UnixMilli()

	stdoutBuffer := bufio.NewWriter(os.Stdout)
	defer func(stdoutBuffer *bufio.Writer) {
		err := stdoutBuffer.Flush()
		if err != nil {
			log.Errorf("Failed to flush stdout: %v", err)
		}
	}(stdoutBuffer)

	for _, ipset := range cfg.IPSets {
		for _, listName := range ipset.Lists {
			list, err := getListByName(cfg, listName)
			if err != nil {
				return err
			}

			if err := iterateOverList(list, cfg, func(host string) error {
				return printDnsmasqIPSetEntry(cfg, stdoutBuffer, host, domains)
			}); err != nil {
				return err
			}
		}
	}

	log.Infof("Producing dnsmasq configuration took %dms", time.Now().UnixMilli()-startTime)

	return nil
}

// printDnsmasqIPSetEntry prints a single dnsmasq ipset=... entry.
func printDnsmasqIPSetEntry(cfg *config.Config, buffer *bufio.Writer, domain string, domains *DomainStore) error {
	if !utils.IsDNSName(domain) {
		return nil
	}

	sanitizedDomain := sanitizeDomain(domain)

	if collision := domains.GetCollisionDomain(sanitizedDomain); collision != "" {
		log.Warnf("Found collision: \"%s\" and \"%s\" have the same CRC32-hash. "+
			"Routing for both of these domains will be undetermined. "+
			"To fix this, please remove one of these domains", domain, collision)
	}

	associations, hash := domains.GetAssociatedIPSetIndexesForDomain(sanitizedDomain)
	if associations == nil {
		return nil
	}

	if _, err := fmt.Fprintf(buffer, "ipset=/%s/", sanitizedDomain); err != nil {
		return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
	}

	isFirstIPSet := true
	for i := 0; i < domains.ipsetCount; i++ {
		if !associations.Has(i) {
			continue
		}

		if !isFirstIPSet {
			if _, err := buffer.WriteRune(','); err != nil {
				return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
			}
		}

		isFirstIPSet = false

		if _, err := buffer.WriteString(cfg.IPSets[i].IPSetName); err != nil {
			return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
		}
	}

	if _, err := buffer.WriteRune('\n'); err != nil {
		return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
	}

	domains.Forget(hash)

	return nil
}

// appendHost appends a host to the appropriate networks or domain store.
func appendHost(host string, ipsetIndex int, ipsetWriter *networking.IPSetWriter, domainStore *DomainStore, ipCount *int) error {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return nil
	}

	if utils.IsDNSName(line) {
		if domainStore == nil {
			return nil
		}
		domainStore.AssociateDomainWithIPSet(sanitizeDomain(line), ipsetIndex)
	} else {
		if ipsetWriter == nil {
			return nil
		}
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
			defer utils.CloseOrPanic(listFile)

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
