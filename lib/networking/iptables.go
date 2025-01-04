package networking

import (
	"github.com/coreos/go-iptables/iptables"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/valyala/fasttemplate"
	"strconv"
	"strings"
)

type IPTableRules struct {
	ipt   *iptables.IPTables
	ipset *config.IpsetConfig
	rules []*config.IPTablesRule
}

func NewIPTableRules(ipset *config.IpsetConfig) (*IPTableRules, error) {
	protocol := iptables.ProtocolIPv4
	if ipset.IpVersion == config.Ipv6 {
		protocol = iptables.ProtocolIPv6
	}

	if ipt, err := iptables.NewWithProtocol(protocol); err != nil {
		return nil, err
	} else {
		if rules, err := processRules(ipset); err != nil {
			return nil, err
		} else {
			return &IPTableRules{ipt, ipset, rules}, nil
		}
	}
}

func processRules(ipset *config.IpsetConfig) ([]*config.IPTablesRule, error) {
	rules := make([]*config.IPTablesRule, len(ipset.IPTablesRule))

	for i, rule := range ipset.IPTablesRule {
		ruleSpecs := make([]string, len(rule.Rule))

		for j, ruleSpec := range rule.Rule {
			ruleSpecs[j] = processRulePart(ruleSpec, ipset)
		}

		rules[i] = &config.IPTablesRule{
			Chain: processRulePart(rule.Chain, ipset),
			Table: processRulePart(rule.Table, ipset),
			Rule:  ruleSpecs,
		}
	}

	return rules, nil
}

func processRulePart(template string, ipset *config.IpsetConfig) string {
	if !strings.Contains(template, "{{") {
		return template
	}

	t := fasttemplate.New(template, "{{", "}}")
	return t.ExecuteString(map[string]interface{}{
		config.IPTABLES_TMPL_IPSET:    ipset.IpsetName,
		config.IPTABLES_TMPL_FWMARK:   strconv.FormatUint(uint64(ipset.Routing.FwMark), 10),
		config.IPTABLES_TMPL_PRIORITY: strconv.FormatUint(uint64(ipset.Routing.IpRulePriority), 10),
		config.IPTABLES_TMPL_TABLE:    strconv.FormatUint(uint64(ipset.Routing.IpRouteTable), 10),
	})
}

func (i *IPTableRules) AddIfNotExists() error {
	for _, rule := range i.rules {
		exists, err := i.ipt.Exists(rule.Table, rule.Chain, rule.Rule...)
		if err != nil {
			return err
		}
		if exists {
			continue
		}

		log.Infof("Adding iptables rule [%v]", rule)
		if err := i.ipt.Append(rule.Table, rule.Chain, rule.Rule...); err != nil {
			return err
		}
	}
	return nil
}

func (i *IPTableRules) DelIfExists() error {
	for _, rule := range i.rules {
		exists, err := i.ipt.Exists(rule.Table, rule.Chain, rule.Rule...)
		if err != nil {
			return err
		}
		if !exists {
			continue
		}

		log.Infof("Deleting iptables rule [%v]", rule)
		if err := i.ipt.Delete(rule.Table, rule.Chain, rule.Rule...); err != nil {
			return err
		}
	}
	return nil
}

func (i *IPTableRules) CheckRulesExists() (map[*config.IPTablesRule]bool, error) {
	rules := make(map[*config.IPTablesRule]bool)

	for _, rule := range i.rules {
		if exists, err := i.ipt.Exists(rule.Table, rule.Chain, rule.Rule...); err != nil {
			log.Errorf("Checking iptables rule presense [%v] is failed: %v", rule, err)
			return nil, err
		} else {
			log.Debugf("Checking iptables rule presense [%v]: exists=%v", rule, exists)
			rules[rule] = exists
		}
	}

	return rules, nil
}
