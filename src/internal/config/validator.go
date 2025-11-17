package config

import (
	"errors"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"os"
	"strings"
)

func (c *Config) ValidateConfig() error {
	if err := c.validateGeneralConfig(); err != nil {
		return err
	}

	if err := c.validateIPSets(); err != nil {
		return err
	}

	if err := c.validateLists(); err != nil {
		return err
	}

	return nil
}

func (c *Config) validateIPSets() error {
	if c.IPSets == nil {
		return fmt.Errorf("configuration should contain \"ipset\" field")
	}

	for _, ipset := range c.IPSets {
		// Validate ipset name
		if err := ipset.validateIPSet(); err != nil {
			return err
		}

		// Validate interfaces
		if len(ipset.Routing.Interfaces) == 0 {
			return fmt.Errorf("ipset %s routing configuration should contain \"interfaces\" field", ipset.IPSetName)
		}

		// check duplicate interfaces
		if err := checkIsDistinct(ipset.Routing.Interfaces, func(iface string) string { return iface }); err != nil {
			return fmt.Errorf("there are duplicate interfaces in ipset %s: %v", ipset.IPSetName, err)
		}

		if len(ipset.Lists) == 0 {
			return fmt.Errorf("ipset %s should contain at least one list in the \"lists\" array", ipset.IPSetName)
		}

		for _, listName := range ipset.Lists {
			exists := false
			for _, list := range c.Lists {
				if list.ListName == listName {
					exists = true
					break
				}
			}

			if !exists {
				return fmt.Errorf("ipset %s contains unknown list \"%s\"", ipset.IPSetName, listName)
			}
		}
	}

	if err := checkIsDistinct(c.IPSets, func(ipset *IPSetConfig) string { return ipset.IPSetName }); err != nil {
		return fmt.Errorf("there are duplicate ipset names: %v", err)
	}
	if err := checkIsDistinct(c.IPSets, func(ipset *IPSetConfig) int { return ipset.Routing.IpRouteTable }); err != nil {
		return fmt.Errorf("there are duplicate routing tables: %v", err)
	}
	if err := checkIsDistinct(c.IPSets, func(ipset *IPSetConfig) int { return ipset.Routing.IpRulePriority }); err != nil {
		return fmt.Errorf("there are duplicate rule priorities: %v", err)
	}
	if err := checkIsDistinct(c.IPSets, func(ipset *IPSetConfig) uint32 { return ipset.Routing.FwMark }); err != nil {
		return fmt.Errorf("there are duplicate fwmarks: %v", err)
	}

	return nil
}

func (c *Config) validateLists() error {
	for _, list := range c.Lists {
		if err := validateNonEmpty(list.ListName, "list_name"); err != nil {
			return err
		}

		isUrl := list.URL != ""
		isFile := list.File != ""
		isHosts := list.Hosts != nil && len(list.Hosts) > 0

		if !isUrl && !isFile && !isHosts {
			return fmt.Errorf("list %s should contain \"url\", \"file\" or non-empty \"hosts\" field", list.ListName)
		}

		if (isUrl && (isFile || isHosts)) || (isFile && isHosts) {
			return fmt.Errorf("list %s can contain only one of \"url\", \"file\" or \"hosts\" field, but not both", list.ListName)
		}

		if isFile {
			list.File = utils.GetAbsolutePath(list.File, c.GetConfigDir())
			if _, err := os.Stat(list.File); errors.Is(err, os.ErrNotExist) {
				return fmt.Errorf("list %s file \"%s\" does not exist", list.ListName, list.File)
			}
		}
	}

	if err := checkIsDistinct(c.Lists, func(list *ListSource) string { return list.ListName }); err != nil {
		return fmt.Errorf("there are duplicate list names: %v", err)
	}

	return nil
}

func (c *Config) validateGeneralConfig() error {
	if c.General == nil {
		return fmt.Errorf("configuration should contain \"general\" field")
	}

	if c.General.UseKeeneticDNS == nil {
		def := false
		c.General.UseKeeneticDNS = &def
	}

	return nil
}

func (ipset *IPSetConfig) validateIPSet() error {
	if err := validateNonEmpty(ipset.IPSetName, "ipset_name"); err != nil {
		return err
	}
	if !ipsetRegexp.MatchString(ipset.IPSetName) {
		return fmt.Errorf("ipset name should consist only of lowercase [a-z0-9_]")
	}

	if ipset.Routing == nil {
		return fmt.Errorf("ipset %s should contain [ipset.routing] field", ipset.IPSetName)
	}

	// Validate IP version
	if newVersion, err := validateIpVersion(ipset.IPVersion); err != nil {
		return err
	} else {
		ipset.IPVersion = newVersion
	}

	// Validate iptables rules
	if err := ipset.validateOrPrefillIPTablesRules(); err != nil {
		return err
	}

	// Validate DNS override format
	if ipset.Routing.DNSOverride != "" {
		if err := validateDNSOverride(ipset.Routing.DNSOverride); err != nil {
			return fmt.Errorf("ipset %s DNS override validation failed: %v", ipset.IPSetName, err)
		}
	}

	return nil
}

func (ipset *IPSetConfig) validateOrPrefillIPTablesRules() error {
	if ipset.IPTablesRules == nil {
		ipset.IPTablesRules = []*IPTablesRule{
			{
				Chain: "PREROUTING",
				Table: "mangle",
				Rule: []string{
					"-m", "mark", "--mark", "0x0/0xffffffff", "-m", "set", "--match-set", "{{" + IPTABLES_TMPL_IPSET + "}}", "dst,src", "-j", "MARK", "--set-mark", "{{" + IPTABLES_TMPL_FWMARK + "}}",
				},
			},
		}

		return nil
	}

	if len(ipset.IPTablesRules) > 0 {
		for _, rule := range ipset.IPTablesRules {
			if rule.Chain == "" {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"chain\" field", ipset.IPSetName)
			}
			if rule.Table == "" {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"table\" field", ipset.IPSetName)
			}
			if len(rule.Rule) == 0 {
				return fmt.Errorf("ipset %s iptables rule should contain non-empty \"rule\" field", ipset.IPSetName)
			}
		}
	}

	return nil
}

func checkIsDistinct[U, T comparable](list []U, mapper func(U) T) error {
	seen := make(map[T]bool)

	for _, item := range list {
		t := mapper(item)
		if seen[t] {
			return fmt.Errorf("value \"%v\" is used more than once", t)
		}
		seen[t] = true
	}

	return nil
}

func validateNonEmpty(value, fieldName string) error {
	if value == "" {
		return fmt.Errorf("%s cannot be empty", fieldName)
	}
	return nil
}

func validateIpVersion(version IpFamily) (IpFamily, error) {
	switch version {
	case Ipv4, Ipv6:
		return version, nil
	default:
		return 0, fmt.Errorf("unknown IP version %d", version)
	}
}

func validateDNSOverride(dnsOverride string) error {
	if dnsOverride == "" {
		return nil
	}

	// Check if it contains a port
	if portIndex := strings.LastIndex(dnsOverride, "#"); portIndex != -1 {
		ip := dnsOverride[:portIndex]
		port := dnsOverride[portIndex+1:]
		
		// Validate IP address
		if !utils.IsIP(ip) {
			return fmt.Errorf("invalid IP address: %s", ip)
		}
		
		// Validate port
		if !utils.IsValidPort(port) {
			return fmt.Errorf("invalid port: %s", port)
		}
	} else {
		// No port specified, just validate IP
		if !utils.IsIP(dnsOverride) {
			return fmt.Errorf("invalid IP address: %s", dnsOverride)
		}
	}

	return nil
}
