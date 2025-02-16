package lists

import (
	"github.com/maksimkurb/keen-pbr/lib/config"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/networking"
	"github.com/maksimkurb/keen-pbr/lib/utils"
)

// ImportListsToIPSets processes the configuration and applies the lists to the appropriate ipsets.
func ImportListsToIPSets(cfg *config.Config) error {
	for ipsetIndex, ipsetCfg := range cfg.IPSets {
		ipCount := 0

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
