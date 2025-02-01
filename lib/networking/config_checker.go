package networking

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
)

func ValidateConfigInterfaces(c *config.Config, interfaces []Interface) error {
	for _, ipset := range c.IPSets {
		for _, interfaceName := range ipset.Routing.Interfaces {
			if err := validateInterfaceExists(interfaceName, interfaces); err != nil {
				return err
			}
		}
	}

	return nil
}

func validateInterfaceExists(interfaceName string, interfaces []Interface) error {
	for _, iface := range interfaces {
		if iface.Attrs().Name == interfaceName {
			return nil
		}
	}
	return fmt.Errorf("interface '%s' is not exists. Please use `keenetic-pbr interfaces` to show interfaces list and then modify your configuration accordingly", interfaceName)
}
