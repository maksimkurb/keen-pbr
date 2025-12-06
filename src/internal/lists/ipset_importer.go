package lists

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// ImportListsToIPSets processes the configuration and applies the lists to the appropriate ipsets.
func ImportListsToIPSets(cfg *config.Config, listManager *Manager) error {
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
				log.Errorf("[ipset %s] Failed to flush: %v", ipsetCfg.IPSetName, err)
			} else {
				log.Debugf("[ipset %s] Flushed", ipsetCfg.IPSetName)
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
		ipv4Count := 0
		ipv6Count := 0
		log.Debugf("[list %s] Processing (ipsets: %v)...", listName, ipsets)
		list, err := getListByName(cfg, listName)
		if err != nil {
			return err
		}

		if err := iterateOverList(list, cfg, func(host string) error {
			isIPv4, isIPv6, err := appendIPOrCIDR(host, ipsets, &ipCount)
			if isIPv4 {
				ipv4Count++
			}
			if isIPv6 {
				ipv6Count++
			}
			return err
		}); err != nil {
			return err
		}

		// Update statistics cache once after processing
		if listManager != nil {
			listManager.UpdateStatistics(list, cfg, 0, ipv4Count, ipv6Count)
		}

		log.Debugf("[list %s] Processing finished: %d IPs/networks loaded to ipset", listName, ipCount)
	}

	log.Infof("All IPs/networks loaded to ipsets")
	return nil
}
