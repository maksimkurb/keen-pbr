package commands

import (
	"flag"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

func CreateUndoCommand() *UndoCommand {
	gc := &UndoCommand{
		fs: flag.NewFlagSet("undo-routing", flag.ExitOnError),
	}
	return gc
}

type UndoCommand struct {
	fs  *flag.FlagSet
	ctx *AppContext
	cfg *config.Config
}

func (g *UndoCommand) Name() string {
	return g.fs.Name()
}

func (g *UndoCommand) Init(args []string, ctx *AppContext) error {
	g.ctx = ctx

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

func (g *UndoCommand) Run() error {
	// Create service layer dependencies
	ipsetMgr := networking.NewIPSetManager()
	networkMgr := networking.NewManager(keenetic.GetDefaultClient())

	routingService := service.NewRoutingService(networkMgr, ipsetMgr)

	// Use RoutingService to undo all routing configuration
	return routingService.Undo(g.cfg)
}
