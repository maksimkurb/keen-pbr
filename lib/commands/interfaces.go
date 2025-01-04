package commands

import (
	"flag"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
)

func CreateInterfacesCommand() *InterfacesCommand {
	gc := &InterfacesCommand{
		fs: flag.NewFlagSet("interfaces", flag.ExitOnError),
	}
	return gc
}

type InterfacesCommand struct {
	fs  *flag.FlagSet
	ctx *AppContext
	cfg *config.Config
}

func (g *InterfacesCommand) Name() string {
	return g.fs.Name()
}

func (g *InterfacesCommand) Init(args []string, ctx *AppContext) error {
	g.ctx = ctx

	if err := g.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath, ctx.Interfaces); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	return nil
}

func (g *InterfacesCommand) Run() error {
	networking.PrintInterfaces(g.ctx.Interfaces, true, *g.cfg.General.UseKeeneticAPI)

	return nil
}
