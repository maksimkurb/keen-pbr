package commands

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy/upstreams"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

type DNSCommand struct {
	ctx  *AppContext
	deps *domain.AppDependencies
}

func CreateDNSCommand() *DNSCommand {
	return &DNSCommand{}
}

func (c *DNSCommand) Name() string {
	return "dns"
}

func (c *DNSCommand) Init(args []string, ctx *AppContext) error {
	c.ctx = ctx
	// Initialize dependencies
	c.deps = domain.NewDefaultDependencies()
	return nil
}

func (c *DNSCommand) Run() error {
	cfg, err := loadConfigOrFail(c.ctx.ConfigPath)
	if err != nil {
		return err
	}

	if cfg.General.DNSServer == nil {
		fmt.Println("No DNS server configuration found")
		return nil
	}

	fmt.Println("Global Upstreams:")
	for _, upstreamURL := range cfg.General.DNSServer.Upstreams {
		upstream, provider, err := upstreams.ParseUpstream(upstreamURL, c.deps.KeeneticClient(), "")
		if err != nil {
			fmt.Printf("  - Error parsing %s: %v\n", upstreamURL, err)
			continue
		}

		if upstream != nil {
			for _, s := range upstream.GetDNSStrings() {
				fmt.Printf("  - %s\n", s)
			}
			utils.CloseOrWarn(upstream)
		}
		if provider != nil {
			// For providers, we need to get upstreams to get the strings
			provUpstreams, err := provider.GetUpstreams()
			if err != nil {
				fmt.Printf("  - Error fetching from provider %s: %v\n", upstreamURL, err)
			} else {
				for _, u := range provUpstreams {
					for _, s := range u.GetDNSStrings() {
						fmt.Printf("  - %s\n", s)
					}
					utils.CloseOrWarn(u)
				}
			}
			utils.CloseOrWarn(provider)
		}
	}

	// Check per-ipset upstreams
	foundIpsetUpstreams := false
	for _, ipset := range cfg.IPSets {
		if ipset.Routing != nil && ipset.Routing.DNS != nil && len(ipset.Routing.DNS.Upstreams) > 0 {
			if !foundIpsetUpstreams {
				fmt.Println("\nIPSet Upstreams:")
				foundIpsetUpstreams = true
			}
			fmt.Printf("  %s:\n", ipset.IPSetName)

			for _, upstreamURL := range ipset.Routing.DNS.Upstreams {
				upstream, provider, err := upstreams.ParseUpstream(upstreamURL, c.deps.KeeneticClient(), "")
				if err != nil {
					fmt.Printf("    - Error parsing %s: %v\n", upstreamURL, err)
					continue
				}

				if upstream != nil {
					for _, s := range upstream.GetDNSStrings() {
						fmt.Printf("    - %s\n", s)
					}
					utils.CloseOrWarn(upstream)
				}
				if provider != nil {
					provUpstreams, err := provider.GetUpstreams()
					if err != nil {
						fmt.Printf("    - Error fetching from provider %s: %v\n", upstreamURL, err)
					} else {
						for _, u := range provUpstreams {
							for _, s := range u.GetDNSStrings() {
								fmt.Printf("    - %s\n", s)
							}
							utils.CloseOrWarn(u)
						}
					}
					utils.CloseOrWarn(provider)
				}
			}
		}
	}

	return nil
}
