package lists

import (
	"bufio"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"net/netip"
	"os"
	"strings"
)

type DestIPSet struct {
	Index  int
	Name   string
	Writer *networking.IPSetWriter
}

func (ips DestIPSet) String() string {
	return ips.Name
}

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

// appendDomain appends a domain to the appropriate networks or domain store.
func appendDomain(host string, ipsets []DestIPSet, domainStore *DomainStore) error {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return nil
	}

	if utils.IsDNSName(line) {
		if domainStore == nil {
			return nil
		}
		domainStore.AssociateDomainWithIPSets(sanitizeDomain(line), ipsets)
	}

	return nil
}

// appendIPOrCIDR appends a host to the appropriate networks or domain store.
func appendIPOrCIDR(host string, ipsets []DestIPSet, ipCount *int) error {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return nil
	}

	if utils.IsDNSName(line) {
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

		for _, ipset := range ipsets {
			if (netPrefix.Addr().Is4() && ipset.Writer.GetIPSet().IpFamily == config.Ipv4) ||
				(netPrefix.Addr().Is6() && ipset.Writer.GetIPSet().IpFamily == config.Ipv6) {
				if err := ipset.Writer.Add(netPrefix); err != nil {
					return err
				}
			}
		}

		*ipCount++
	} else {
		log.Warnf("Could not parse host, skipping: %s", host)
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

func getListByName(cfg *config.Config, listName string) (*config.ListSource, error) {
	for _, l := range cfg.Lists {
		if l.ListName == listName {
			return l, nil
		}
	}
	return nil, fmt.Errorf("list \"%s\" not found", listName)
}
