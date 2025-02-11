package lists

import (
	"bufio"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
	"os"
	"time"
)

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
