package networking

import (
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/vishvananda/netlink"
)

type IpRule struct {
	*netlink.Rule
}

func (r *IpRule) String() string {
	from := "all"
	if r.Src != nil && r.Src.String() != "<nil>" {
		from = r.Src.String()
	}

	to := "all"
	if r.Dst != nil && r.Dst.String() != "<nil>" {
		to = r.Dst.String()
	}

	return fmt.Sprintf("rule %d: from %s to %s fwmark=%d -> table %d",
		r.Priority, from, to, r.Mark, r.Table)
}

func BuildRule(ipFamily config.IpFamily, fwmark uint32, table int, priority int) *IpRule {
	ipr := netlink.NewRule()

	ipr.Table = table
	ipr.Mark = fwmark
	ipr.Priority = priority
	if ipFamily == config.Ipv6 {
		ipr.Family = netlink.FAMILY_V6
	} else {
		ipr.Family = netlink.FAMILY_V4
	}
	return &IpRule{ipr}
}

func (ipr *IpRule) Add() error {
	log.Debugf("Adding IP rule [%v]", ipr)
	if err := netlink.RuleAdd(ipr.Rule); err != nil {
		log.Warnf("Failed to add IP rule [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRule) AddIfNotExists() (bool, error) {
	if exists, err := ipr.IsExists(); err != nil {
		return false, err
	} else {
		if !exists {
			if err := ipr.Add(); err != nil {
				return false, err
			}
			return true, nil
		}
	}
	return false, nil
}

func (ipr *IpRule) IsExists() (bool, error) {
	if filtered, err := netlink.RuleListFiltered(ipr.Family, ipr.Rule, netlink.RT_FILTER_TABLE|netlink.RT_FILTER_MARK|netlink.RT_FILTER_PRIORITY); err != nil {
		log.Warnf("Checking if IP rule exists [%v] is failed: %v", ipr, err)
		return false, err
	} else {
		if len(filtered) > 0 {
			log.Debugf("Checking if IP rule exists [%v]: YES", ipr)
			return true, nil
		}
	}

	log.Debugf("Checking if IP rule exists [%v]: NO", ipr)
	return false, nil
}

func (ipr *IpRule) Del() error {
	log.Debugf("Deleting IP rule [%v]", ipr)
	if err := netlink.RuleDel(ipr.Rule); err != nil {
		log.Warnf("Failed to delete IP rule [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRule) DelIfExists() (bool, error) {
	if exists, err := ipr.IsExists(); err != nil {
		return false, err
	} else {
		if exists {
			if err := ipr.Del(); err != nil {
				return false, err
			}
			return true, nil
		}
	}
	return false, nil
}
