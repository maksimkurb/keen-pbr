package lists

import (
	"bufio"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"os"
	"time"
)

// PrintDnsmasqConfig processes the configuration and prints the dnsmasq configuration.
func PrintDnsmasqConfig(cfg *config.Config, keeneticClient *keenetic.Client, listManager *Manager) error {
	if err := CreateIPSetsIfAbsent(cfg); err != nil {
		return err
	}

	domainStore := CreateDomainStore(len(cfg.IPSets))
	listMapping := make(map[string][]DestIPSet)
	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		for _, listName := range ipsetCfg.Lists {
			if listMapping[listName] == nil {
				listMapping[listName] = make([]DestIPSet, 0)
			}

			listMapping[listName] = append(listMapping[listName], DestIPSet{
				Index: ipsetIndex,
				Name:  ipsetCfg.IPSetName,
			})
		}
	}

	for listName, ipsets := range listMapping {
		log.Infof("Processing list \"%s\" (ipsets: %v)...", listName, ipsets)
		list, err := getListByName(cfg, listName)
		if err != nil {
			return err
		}

		// Start recording statistics for this list
		if listManager != nil {
			listManager.StartListProcessing(list, cfg)
		}

		if err := iterateOverList(list, cfg, func(host string) error {
			isDomain, err := appendDomain(host, ipsets, domainStore)
			if listManager != nil && isDomain {
				listManager.RecordLineProcessed(list, cfg, host, true, false, false)
			}
			return err
		}); err != nil {
			return err
		}

		// Finish recording statistics for this list
		if listManager != nil {
			downloaded, lastModified := getFileStats(list, cfg)
			listManager.FinishListProcessing(list, cfg, downloaded, lastModified)
		}
	}

	log.Infof("Parsed %d domains. Producing dnsmasq config into stdout...", domainStore.Count())

	if domainStore.Count() > 0 {
		if err := printDnsmasqConfig(cfg, domainStore, keeneticClient); err != nil {
			return err
		}
	}

	return nil
}

// printDnsmasqConfig writes the dnsmasq configuration to stdout.
func printDnsmasqConfig(cfg *config.Config, domains *DomainStore, keeneticClient *keenetic.Client) error {
	startTime := time.Now().UnixMilli()

	stdoutBuffer := bufio.NewWriter(os.Stdout)
	defer func(stdoutBuffer *bufio.Writer) {
		err := stdoutBuffer.Flush()
		if err != nil {
			log.Errorf("Failed to flush stdout: %v", err)
		}
	}(stdoutBuffer)

	if *cfg.General.UseKeeneticDNS {
		// Import keenetic DNS servers
		var keeneticServers []keenetic.DnsServerInfo
		var err error
		if keeneticClient != nil {
			keeneticServers, err = keeneticClient.GetDNSServers()
		} else {
			err = fmt.Errorf("keenetic client not available")
		}
		if err != nil {
			if cfg.General.FallbackDNS != "" {
				log.Warnf("Failed to fetch Keenetic DNS servers, using fallback DNS: %s", cfg.General.FallbackDNS)
				row := "server=" + cfg.General.FallbackDNS + "\n"
				if _, err := stdoutBuffer.WriteString(row); err != nil {
					return fmt.Errorf("failed to print fallback DNS to dnsmasq cfg file: %v", err)
				}
			} else {
				log.Warnf("Failed to fetch Keenetic DNS servers, no fallback DNS provided")
			}
		} else {
			log.Infof("Found %d Keenetic DNS servers", len(keeneticServers))
			for _, server := range keeneticServers {
				ip := server.Proxy
				port := server.Port

				row := "server="
				if server.Domain != nil && *server.Domain != "" {
					row += "/" + *server.Domain + "/"
				}
				row += ip
				if port != "" {
					row += "#" + port
				}
				if _, err := stdoutBuffer.WriteString(row + "\n"); err != nil {
					return fmt.Errorf("failed to print DNS to dnsmasq cfg file: %v", err)
				}
			}
		}
	}

	for _, ipset := range cfg.IPSets {
		for _, listName := range ipset.Lists {
			list, err := getListByName(cfg, listName)
			if err != nil {
				return err
			}

			if err := iterateOverList(list, cfg, func(host string) error {
				return printDnsmasqIPSetEntry(cfg, stdoutBuffer, host, domains, ipset)
			}); err != nil {
				return err
			}
		}
	}

	log.Infof("Producing dnsmasq configuration took %dms", time.Now().UnixMilli()-startTime)

	return nil
}

// printDnsmasqIPSetEntry prints a single dnsmasq ipset=... entry.
func printDnsmasqIPSetEntry(cfg *config.Config, buffer *bufio.Writer, domain string, domains *DomainStore, ipset *config.IPSetConfig) error {
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

	// Handle DNS override for this domain if specified
	if ipset.Routing != nil && ipset.Routing.DNSOverride != "" {
		if _, err := fmt.Fprintf(buffer, "server=/%s/%s\n", sanitizedDomain, ipset.Routing.DNSOverride); err != nil {
			return fmt.Errorf("failed to write DNS override to dnsmasq cfg file: %v", err)
		}
	}

	domains.Forget(hash)

	return nil
}
