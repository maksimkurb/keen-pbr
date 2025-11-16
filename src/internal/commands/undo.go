package commands

import (
	"flag"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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
	log.Infof("Removing all iptables rules, ip rules and ip routes...")

	for _, ipset := range g.cfg.IPSets {
		if err := undoIpset(ipset); err != nil {
			log.Errorf("Failed to undo routing configuration for ipset [%s]: %v", ipset.IPSetName, err)
			return err
		}
	}

	log.Infof("Undo routing completed successfully")
	return nil
}

func undoIpset(ipset *config.IPSetConfig) error {
	log.Infof("----------------- IPSet [%s] ------------------", ipset.IPSetName)

	log.Infof("Deleting IP route table %d", ipset.Routing.IpRouteTable)
	if err := networking.DelIpRouteTable(ipset.Routing.IpRouteTable); err != nil {
		return err
	}

	ipRule := networking.BuildIPRuleForIpset(ipset)
	log.Infof("Deleting IP rule [%v]", ipRule)
	if exists, err := ipRule.IsExists(); err != nil {
		log.Errorf("Failed to check IP rule [%v]: %v", ipRule, err)
		return err
	} else {
		if exists {
			if err := ipRule.DelIfExists(); err != nil {
				log.Errorf("Failed to delete IP rule [%v]: %v", ipRule, err)
				return err
			}
		} else {
			log.Infof("IP rule [%v] is NOT exists, skipping", ipRule)
		}
	}

	log.Infof("Deleting iptable rules")
	if ipTableRules, err := networking.BuildIPTablesForIpset(ipset); err != nil {
		log.Errorf("Failed to build iptable rules: %v", err)
	} else {
		if err := ipTableRules.DelIfExists(); err != nil {
			log.Errorf("Failed to delete iptable rules [%v]: %v", ipTableRules, err)
			return err
		}
	}

	log.Infof("----------------- IPSet [%s] END ------------------", ipset.IPSetName)
	return nil
}
