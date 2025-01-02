package commands

import (
	"flag"
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
}

func (g *InterfacesCommand) Name() string {
	return g.fs.Name()
}

func (g *InterfacesCommand) Init(args []string, ctx *AppContext) error {
	g.ctx = ctx

	return g.fs.Parse(args)
}

func (g *InterfacesCommand) Run() error {
	networking.PrintInterfaces(g.ctx.Interfaces, true)

	return nil
}
