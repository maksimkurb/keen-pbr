package commands

import (
	"flag"
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

func CreateInterfacesCommand() *InterfacesCommand {
	gc := &InterfacesCommand{
		fs: flag.NewFlagSet("interfaces", flag.ExitOnError),
	}
	return gc
}

type InterfacesCommand struct {
	fs   *flag.FlagSet
	ctx  *AppContext
	cfg  *config.Config
	deps *domain.AppDependencies
}

func (g *InterfacesCommand) Name() string {
	return g.fs.Name()
}

func (g *InterfacesCommand) Init(args []string, ctx *AppContext) error {
	g.ctx = ctx

	if err := g.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	// Initialize dependencies
	g.deps = domain.NewDefaultDependencies()

	return nil
}

func (g *InterfacesCommand) Run() error {
	// Use shared InterfaceService
	ifaceService := service.NewInterfaceService(g.deps.KeeneticClient())
	interfaces, err := ifaceService.GetInterfacesFromList(g.ctx.Interfaces, true)
	if err != nil {
		return fmt.Errorf("failed to get interfaces: %v", err)
	}

	// Use shared formatting
	fmt.Print(ifaceService.FormatInterfacesForCLI(interfaces))
	return nil
}
