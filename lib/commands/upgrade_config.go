package commands

import (
	"flag"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
)

func CreateUpgradeConfigCommand() *UpgradeConfigCommand {
	gc := &UpgradeConfigCommand{
		fs: flag.NewFlagSet("upgrade-config", flag.ExitOnError),
	}
	return gc
}

type UpgradeConfigCommand struct {
	fs  *flag.FlagSet
	ctx *AppContext
	cfg *config.Config
}

func (g *UpgradeConfigCommand) Name() string {
	return g.fs.Name()
}

func (g *UpgradeConfigCommand) Init(args []string, ctx *AppContext) error {
	g.ctx = ctx

	if err := g.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := config.LoadConfig(ctx.ConfigPath); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	return nil
}

func (g *UpgradeConfigCommand) Run() error {
	log.Infof("Upgrading configuration version...")

	if upgraded, err := g.cfg.UpgradeConfig(); err != nil {
		log.Errorf("Failed to upgrade config: %v", err)
		return err
	} else if upgraded {
		if err := g.cfg.WriteConfig(); err != nil {
			log.Errorf("Failed to write config: %v", err)
			return err
		}

		log.Infof("Updated configuration is written on disk!")
	} else {
		log.Infof("Configuration does not need to be upgraded")
	}
	return nil
}
