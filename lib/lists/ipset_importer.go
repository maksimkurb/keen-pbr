package lists

import (
	"github.com/maksimkurb/keen-pbr/lib/config"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/networking"
)

// ImportListsToIPSets processes the configuration and applies the lists to the appropriate ipsets.
func ImportListsToIPSets(cfg *config.Config) error {
	listMapping := make(map[string][]DestIPSet)
	defer func() {
		for _, ipsets := range listMapping {
			for _, ipset := range ipsets {
				if err := ipset.Writer.Close(); err != nil {
					log.Errorf("Failed to close ipset \"%s\": %v", ipset.Name, err)
				}
			}
		}
	}()

	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		ipset := networking.BuildIPSet(ipsetCfg.IPSetName, ipsetCfg.IPVersion)

		if err := ipset.CreateIfNotExists(); err != nil {
			return err
		}

		if ipsetCfg.FlushBeforeApplying {
			if err := ipset.Flush(); err != nil {
				log.Errorf("Failed to flush ipset \"%s\": %v", ipsetCfg.IPSetName, err)
			} else {
				log.Infof("Flushed ipset \"%s\"", ipsetCfg.IPSetName)
			}
		}

		for _, listName := range ipsetCfg.Lists {
			if listMapping[listName] == nil {
				listMapping[listName] = make([]DestIPSet, 0)
			}

			if ipsetWriter, err := ipset.OpenWriter(); err != nil {
				return err
			} else {
				listMapping[listName] = append(listMapping[listName], DestIPSet{
					Index:  ipsetIndex,
					Name:   ipsetCfg.IPSetName,
					Writer: ipsetWriter,
				})
			}
		}
	}

	for listName, ipsets := range listMapping {
		ipCount := 0
		log.Infof("Processing list \"%s\" (ipsets: %v)...", listName, ipsets)
		list, err := getListByName(cfg, listName)
		if err != nil {
			return err
		}

		if err := iterateOverList(list, cfg, func(host string) error {
			return appendIPOrCIDR(host, ipsets, &ipCount)
		}); err != nil {
			return err
		}

		log.Infof("List \"%s\" processing finished: %d IPs/networks loaded to ipset", listName, ipCount)
	}

	log.Infof("All IPs/networks loaded to ipsets")
	return nil
}
