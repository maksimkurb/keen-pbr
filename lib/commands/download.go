package commands

import (
	"flag"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/lists"
)

func CreateDownloadCommand() *DownloadCommand {
	gc := &DownloadCommand{
		fs: flag.NewFlagSet("download", flag.ExitOnError),
	}
	return gc
}

type DownloadCommand struct {
	fs  *flag.FlagSet
	cfg *config.Config
}

func (g *DownloadCommand) Name() string {
	return g.fs.Name()
}

func (g *DownloadCommand) Init(args []string, ctx *AppContext) error {
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

func (g *DownloadCommand) Run() error {
	return lists.DownloadLists(g.cfg)
}
