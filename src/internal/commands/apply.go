package commands

import (
	"flag"
	"fmt"
	"os"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

func CreateApplyCommand() *ApplyCommand {
	gc := &ApplyCommand{
		fs: flag.NewFlagSet("apply", flag.ExitOnError),
	}

	gc.fs.BoolVar(&gc.SkipIpset, "skip-ipset", false, "Skip ipset filling")
	gc.fs.BoolVar(&gc.SkipRouting, "skip-routing", false, "Skip ip routes and ip rules applying")
	gc.fs.StringVar(&gc.OnlyRoutingForInterface, "only-routing-for-interface", "", "Only apply ip routes/rules for the specified interface (if it is present in keen-pbr config)")
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
	// Create service layer dependencies
	ipsetMgr := networking.NewIPSetManager()
	networkMgr := networking.NewManager(keenetic.GetDefaultClient())

	ipsetService := service.NewIPSetService(ipsetMgr)
	routingService := service.NewRoutingService(networkMgr, ipsetMgr)

	// Handle ipset operations
	if !g.SkipIpset {
		// Always ensure ipsets exist
		if err := ipsetService.EnsureIPSetsExist(g.cfg); err != nil {
			return fmt.Errorf("failed to create ipsets: %v", err)
		}

		// Only populate ipsets if we're not doing targeted routing for a single interface
		if g.OnlyRoutingForInterface == "" {
			if err := ipsetService.PopulateIPSets(g.cfg); err != nil {
				return fmt.Errorf("failed to apply lists: %v", err)
			}
		}
	}

	// Handle routing operations
	if !g.SkipRouting {
		var onlyInterface *string
		if g.OnlyRoutingForInterface != "" {
			onlyInterface = &g.OnlyRoutingForInterface
		}

		opts := service.ApplyOptions{
			SkipIPSet:     g.SkipIpset,
			OnlyInterface: onlyInterface,
		}

		if err := routingService.Apply(g.cfg, opts); err != nil {
			return fmt.Errorf("failed to apply routing: %v", err)
		}

		// Check if anything was actually applied when using OnlyInterface
		if onlyInterface != nil {
			// Filter to see if any ipsets match this interface
			appliedAny := false
			for _, ipsetCfg := range g.cfg.IPSets {
				if ipsetCfg.Routing != nil {
					for _, iface := range ipsetCfg.Routing.Interfaces {
						if iface == *onlyInterface {
							appliedAny = true
							break
						}
					}
				}
				if appliedAny {
					break
				}
			}

			if !appliedAny {
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
