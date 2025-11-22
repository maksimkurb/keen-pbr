package lists

import (
	"bufio"
	"fmt"
	"net/netip"
	"os"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

type DestIPSet struct {
	Index  int
	Name   string
	Writer *networking.IPSetWriter
}

func (ips DestIPSet) String() string {
	return ips.Name
}

// appendIPOrCIDR appends a host to the appropriate networks or domain store.
// Returns (isIPv4, isIPv6, error).
func appendIPOrCIDR(host string, ipsets []DestIPSet, ipCount *int) (bool, bool, error) {
	line := strings.TrimSpace(host)
	if line == "" || strings.HasPrefix(line, "#") {
		return false, false, nil
	}

	if utils.IsDNSName(line) {
		return false, false, nil
	}

	if strings.LastIndex(line, "/") < 0 {
		line = line + "/32"
	}
	if netPrefix, err := netip.ParsePrefix(line); err == nil {
		if !netPrefix.IsValid() {
			log.Warnf("Could not parse host, skipping: %s", host)
			return false, false, nil
		}

		isIPv4 := netPrefix.Addr().Is4()
		isIPv6 := netPrefix.Addr().Is6()

		for _, ipset := range ipsets {
			if (isIPv4 && ipset.Writer.GetIPSet().IPFamily == config.Ipv4) ||
				(isIPv6 && ipset.Writer.GetIPSet().IPFamily == config.Ipv6) {
				if err := ipset.Writer.Add(netPrefix); err != nil {
					return false, false, err
				}
			}
		}

		*ipCount++
		return isIPv4, isIPv6, nil
	} else {
		log.Warnf("Could not parse host, skipping: %s", host)
	}

	return false, false, nil
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

// GetNetworksFromList parses a list and returns all valid network prefixes.
//
// This is a public helper for the service layer to get networks from a list source.
// It parses each line, skipping comments, DNS names, and invalid entries.
func GetNetworksFromList(list *config.ListSource, cfg *config.Config) ([]netip.Prefix, error) {
	var networks []netip.Prefix

	err := iterateOverList(list, cfg, func(host string) error {
		line := strings.TrimSpace(host)
		if line == "" || strings.HasPrefix(line, "#") {
			return nil
		}

		// Skip DNS names
		if utils.IsDNSName(line) {
			return nil
		}

		// Add /32 for single IPs
		if strings.LastIndex(line, "/") < 0 {
			line = line + "/32"
		}

		// Parse network prefix
		if netPrefix, err := netip.ParsePrefix(line); err == nil {
			if netPrefix.IsValid() {
				networks = append(networks, netPrefix)
			} else {
				log.Warnf("Invalid network prefix, skipping: %s", host)
			}
		} else {
			log.Warnf("Could not parse network prefix, skipping: %s", host)
		}

		return nil
	})

	return networks, err
}
