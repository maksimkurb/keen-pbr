package commands

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

type DNSCommand struct{}

func CreateDNSCommand() *DNSCommand {
	return &DNSCommand{}
}

func (c *DNSCommand) Name() string {
	return "dns"
}

func (c *DNSCommand) Init(args []string, ctx *AppContext) error {
	return nil
}

func (c *DNSCommand) Run() error {
	client := keenetic.NewClient(nil)
	servers, err := client.GetDNSServers()
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
