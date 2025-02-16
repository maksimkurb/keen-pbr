package lists

import (
	"bufio"
	"fmt"
	"github.com/maksimkurb/keen-pbr/lib/config"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/networking"
	"github.com/maksimkurb/keen-pbr/lib/utils"
	"net/netip"
	"os"
	"strings"
)

// CreateIPSetsIfAbsent creates the ipsets if they do not exist.
func CreateIPSetsIfAbsent(cfg *config.Config) error {
	for _, ipsetCfg := range cfg.IPSets {
		ipset := networking.BuildIPSet(ipsetCfg.IPSetName, ipsetCfg.IPVersion)
		if err := ipset.CreateIfNotExists(); err != nil {
			return err
		}
	}

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
