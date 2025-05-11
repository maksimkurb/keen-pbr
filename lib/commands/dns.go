package commands

import (
	"fmt"
	"github.com/maksimkurb/keen-pbr/lib/keenetic"
)

type DnsCommand struct{}

func CreateDnsCommand() *DnsCommand {
	return &DnsCommand{}
}

func (c *DnsCommand) Name() string {
	return "dns"
}

func (c *DnsCommand) Init(args []string, ctx *AppContext) error {
	return nil
}

func (c *DnsCommand) Run() error {
	servers, err := keenetic.RciShowDnsServers()
	if err != nil {
		return fmt.Errorf("failed to fetch DNS servers: %v", err)
	}
	for _, server := range servers {
		domain := "-"
		if server.Domain != nil {
			domain = *server.Domain
		}
		if server.Port != "" {
			fmt.Printf("  [%s] %-35s [for domain: %-15s] %s:%s\n", server.Type, server.Endpoint, domain, server.Proxy, server.Port)
		} else {
			fmt.Printf("  [%s] %-35s [for domain: %-15s] %s\n", server.Type, server.Endpoint, domain, server.Proxy)
		}
	}
	return nil
}
