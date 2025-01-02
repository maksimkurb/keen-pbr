package networking

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/vishvananda/netlink"
	"log"
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

	return fmt.Sprintf("%d: from %s to %s fwmark=%d -> table %d",
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
	log.Printf("Adding IP rule [%v]", ipr)
	if err := netlink.RuleAdd(ipr.Rule); err != nil {
		log.Printf("Failed to add IP rule [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRule) AddIfNotExists() error {
	if exists, err := ipr.IsExists(); err != nil {
		return err
	} else {
		if !exists {
			return ipr.Add()
		}
	}
	return nil
}

func (ipr *IpRule) IsExists() (bool, error) {
	if filtered, err := netlink.RuleListFiltered(ipr.Family, ipr.Rule, netlink.RT_FILTER_TABLE|netlink.RT_FILTER_MARK|netlink.RT_FILTER_PRIORITY); err != nil {
		log.Printf("Checking if IP rule exists [%v] is failed: %v", ipr, err)
		return false, err
	} else {
		if len(filtered) > 0 {
			log.Printf("Checking if IP rule exists [%v]: YES", ipr)
			return true, nil
		}
	}

	log.Printf("Checking if IP rule exists [%v]: NO", ipr)
	return false, nil
}

func (ipr *IpRule) Del() error {
	log.Printf("Deleting IP rule [%v]", ipr)
	if err := netlink.RuleDel(ipr.Rule); err != nil {
		log.Printf("Failed to delete IP rule [%v]: %v", ipr, err)
		return err
	}

	return nil
}

func (ipr *IpRule) DelIfExists() error {
	if exists, err := ipr.IsExists(); err != nil {
		return err
	} else {
		if exists {
			return ipr.Del()
		}
	}
	return nil
}
