package commands

import (
	"flag"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

func CreateDnsmasqConfigCommand() *DnsmasqConfigCommand {
	gc := &DnsmasqConfigCommand{
		fs: flag.NewFlagSet("print-dnsmasq-config", flag.ExitOnError),
	}

	return gc
}

type DnsmasqConfigCommand struct {
	fs  *flag.FlagSet
	cfg *config.Config
}

func (g *DnsmasqConfigCommand) Name() string {
	return g.fs.Name()
}

func (g *DnsmasqConfigCommand) Init(args []string, ctx *AppContext) error {
	log.SetForceStdErr(true)

	if err := g.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	return nil
}

func (g *DnsmasqConfigCommand) Run() error {
	client := keenetic.NewClient(nil)
	if err := lists.PrintDnsmasqConfig(g.cfg, client); err != nil {
		return fmt.Errorf("failed to print dnsmasq config: %v", err)
	}

	return nil
}
