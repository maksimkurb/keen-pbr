package networking

import (
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

func ValidateInterfacesArePresent(c *config.Config, interfaces []Interface) error {
	for _, ipset := range c.IPSets {
		hasValidInterface := false
		
		for _, interfaceName := range ipset.Routing.Interfaces {
			if err := validateInterfaceExists(interfaceName, interfaces); err != nil {
				log.Errorf("Interface '%s' for ipset '%s' does not exist", interfaceName, ipset.IPSetName)
			} else {
				hasValidInterface = true
			}
		}
		
		if !hasValidInterface {
			return fmt.Errorf("ipset '%s' has no valid interfaces available", ipset.IPSetName)
		}
	}

	return nil
}

func PrintMissingInterfacesHelp() {
	log.Warnf("(tip) Please enter command `keen-pbr interfaces` to show available interfaces list")
}

func validateInterfaceExists(interfaceName string, interfaces []Interface) error {
	for _, iface := range interfaces {
		if iface.Attrs().Name == interfaceName {
			return nil
		}
	}
	return fmt.Errorf("interface '%s' is not exists", interfaceName)
}
