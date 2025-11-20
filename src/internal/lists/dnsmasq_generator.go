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
func PrintDnsmasqConfig(cfg *config.Config, configPath string, keeneticClient *keenetic.Client, listManager *Manager) error {
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
		log.Infof("[list %s] Processing (ipsets: %v)...", listName, ipsets)
		list, err := getListByName(cfg, listName)
		if err != nil {
			return err
		}

		// Count domains for statistics
		domainCount := 0

		if err := iterateOverList(list, cfg, func(host string) error {
			isDomain, err := appendDomain(host, ipsets, domainStore)
			if isDomain {
				domainCount++
			}
			return err
		}); err != nil {
			// Log error but continue processing other lists
			log.Errorf("[list %s] Failed to process: %v. Skipping this list in dnsmasq config.", listName, err)
			continue
		}

		// Update statistics cache once after processing
		if listManager != nil {
			listManager.UpdateStatistics(list, cfg, domainCount, 0, 0)
		}
	}

	log.Infof("Parsed %d domains. Producing dnsmasq config into stdout...", domainStore.Count())

	if domainStore.Count() > 0 {
		if err := printDnsmasqConfig(cfg, configPath, domainStore, keeneticClient); err != nil {
			return err
		}
	}

	return nil
}

// printDnsmasqConfig writes the dnsmasq configuration to stdout.
func printDnsmasqConfig(cfg *config.Config, configPath string, domains *DomainStore, keeneticClient *keenetic.Client) error {
	startTime := time.Now().UnixMilli()

	stdoutBuffer := bufio.NewWriter(os.Stdout)
	defer func(stdoutBuffer *bufio.Writer) {
		err := stdoutBuffer.Flush()
		if err != nil {
			log.Errorf("Failed to flush stdout: %v", err)
		}
	}(stdoutBuffer)

	// Calculate and print config hash as CNAME record for dnsmasq tracking
	hasher := config.NewConfigHasher(configPath)
	// Set Keenetic client to ensure hash calculation includes DNS servers (if enabled)
	// This ensures consistency with the hash calculated during service startup
	hasher.SetKeeneticClient(keeneticClient)
	configHash, err := hasher.UpdateCurrentConfigHash()
	if err != nil {
		log.Warnf("Failed to calculate config hash: %v", err)
		configHash = "unknown"
	}

	// Print CNAME record: cname=config-md5.keen-pbr.internal,<MD5>.value.keen-pbr.internal,1
	cnameRecord := fmt.Sprintf("cname=config-md5.keen-pbr.internal,%s.value.keen-pbr.internal,1\n", configHash)
	if _, err := stdoutBuffer.WriteString(cnameRecord); err != nil {
		return fmt.Errorf("failed to write config hash CNAME to dnsmasq cfg: %v", err)
	}
	log.Infof("Dnsmasq config hash: %s", configHash)

	// Get upstream DNS servers using centralized resolution logic
	upstreamServers := ResolveDNSServers(cfg, keeneticClient)
	if len(upstreamServers) > 0 {
		log.Infof("Using %d upstream DNS server(s)", len(upstreamServers))
		for _, server := range upstreamServers {
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
	} else {
		log.Infof("No upstream DNS servers configured")
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
				// Log error but continue processing other lists
				log.Errorf("[list %s] [ipset %s] Failed to process: %v. Skipping this list in dnsmasq config.", listName, ipset.IPSetName, err)
				continue
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
