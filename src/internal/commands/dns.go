package commands

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

type DNSCommand struct {
	deps *domain.AppDependencies
}

func CreateDNSCommand() *DNSCommand {
	return &DNSCommand{}
}

func (c *DNSCommand) Name() string {
	return "dns"
}

func (c *DNSCommand) Init(args []string, ctx *AppContext) error {
	// Initialize dependencies
	c.deps = domain.NewDefaultDependencies()
	return nil
}

func (c *DNSCommand) Run() error {
	// Use shared DNSService
	dnsService := service.NewDNSService(c.deps.KeeneticClient())
	servers, err := dnsService.GetDNSServers()
	if err != nil {
		return fmt.Errorf("failed to fetch DNS servers: %v", err)
	}

	// Use shared formatting
	fmt.Print(dnsService.FormatDNSServers(servers))
	return nil
}
