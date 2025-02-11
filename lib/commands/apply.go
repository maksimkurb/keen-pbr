package commands

import (
	"flag"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/lists"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"os"
)

func CreateApplyCommand() *ApplyCommand {
	gc := &ApplyCommand{
		fs: flag.NewFlagSet("apply", flag.ExitOnError),
	}

	gc.fs.BoolVar(&gc.SkipIpset, "skip-ipset", false, "Skip ipset filling")
	gc.fs.BoolVar(&gc.SkipRouting, "skip-routing", false, "Skip ip routes and ip rules applying")
	gc.fs.StringVar(&gc.OnlyRoutingForInterface, "only-routing-for-interface", "", "Only apply ip routes/rules for the specified interface (if it is present in keenetic-pbr config)")
	gc.fs.BoolVar(&gc.FailIfNothingToApply, "fail-if-nothing-to-apply", false, "If there is routing configuration to apply, exit with error code (5)")

	return gc
}

type ApplyCommand struct {
	fs  *flag.FlagSet
	cfg *config.Config

	SkipIpset               bool
	SkipRouting             bool
	OnlyRoutingForInterface string
	FailIfNothingToApply    bool
}

func (g *ApplyCommand) Name() string {
	return g.fs.Name()
}

func (g *ApplyCommand) Init(args []string, ctx *AppContext) error {
	if err := g.fs.Parse(args); err != nil {
		return err
	}

	if g.SkipIpset && g.SkipRouting {
		return fmt.Errorf("--skip-ipset and --skip-routing are used, nothing to do")
	}

	if g.OnlyRoutingForInterface != "" && (g.SkipRouting || g.SkipIpset) {
		return fmt.Errorf("--only-routing-for-interface and --skip-* can not be used together")
	}

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	if !g.SkipRouting || g.OnlyRoutingForInterface != "" {
		if err := networking.ValidateInterfacesArePresent(g.cfg, ctx.Interfaces); err != nil {
			return fmt.Errorf("failed to apply routing: %v", err)
		}
	}

	return nil
}

func (g *ApplyCommand) Run() error {
	if !g.SkipIpset && g.OnlyRoutingForInterface == "" {
		if err := lists.ImportListsToIPSets(g.cfg); err != nil {
			return fmt.Errorf("failed to apply lists: %v", err)
		}
	} else {
		if err := lists.CreateIPSetsIfAbsent(g.cfg); err != nil {
			return fmt.Errorf("failed to create ipsets: %v", err)
		}
	}

	if !g.SkipRouting {
		if appliedAtLeastOnce, err := networking.ApplyNetworkConfiguration(g.cfg, &g.OnlyRoutingForInterface); err != nil {
			return fmt.Errorf("failed to apply routing: %v", err)
		} else {
			if !appliedAtLeastOnce {
				if g.FailIfNothingToApply {
					log.Warnf("Nothing to apply, exiting with exit_code=5")
					os.Exit(5)
				} else {
					log.Warnf("Nothing to apply")
				}
			}
		}
	}

	return nil
}
