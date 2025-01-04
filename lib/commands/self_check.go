package commands

import (
	"encoding/binary"
	"flag"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/keenetic"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"os"
)

func CreateSelfCheckCommand() *SelfCheckCommand {
	gc := &SelfCheckCommand{
		fs: flag.NewFlagSet("self-check", flag.ExitOnError),
	}
	return gc
}

type SelfCheckCommand struct {
	fs  *flag.FlagSet
	ctx *AppContext
	cfg *config.Config
}

func (g *SelfCheckCommand) Name() string {
	return g.fs.Name()
}

func (g *SelfCheckCommand) Init(args []string, ctx *AppContext) error {
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

func (g *SelfCheckCommand) Run() error {

	log.Infof("Running self-check...")
	log.Infof("---------------- Configuration START -----------------")

	if cfg, err := g.cfg.SerializeConfig(); err != nil {
		log.Errorf("Failed to serialize config: %v", err)
		return err
	} else {
		if err := binary.Write(os.Stdout, binary.LittleEndian, cfg); err != nil {
			log.Errorf("Failed to output config: %v", err)
			return err
		}
	}

	log.Infof("----------------- Configuration END ------------------")

	for _, ipset := range g.cfg.Ipset {
		if err := checkIpset(g.cfg, ipset); err != nil {
			log.Errorf("Failed to check ipset routing configuration [%s]: %v", ipset.IpsetName, err)
			return err
		}
	}

	log.Infof("Self-check completed successfully")
	return nil
}

func checkIpset(cfg *config.Config, ipset *config.IpsetConfig) error {
	log.Infof("----------------- IPSet [%s] ------------------", ipset.IpsetName)

	if ipset.Routing.KillSwitch {
		log.Infof("Usage of kill-switch is enabled")
	} else {
		log.Infof("Usage of kill-switch is DISABLED")
	}

	if exists, err := networking.CheckIpsetExists(ipset); err != nil {
		log.Errorf("Failed to check ipset presense [%s]: %v", ipset.IpsetName, err)
		return err
	} else {
		if exists {
			log.Infof("ipset [%s] is exists", ipset.IpsetName)
		} else {
			log.Errorf("ipset [%s] is NOT exists", ipset.IpsetName)
		}
	}

	ipRule := networking.BuildIPRuleForIpset(ipset)
	if exists, err := ipRule.IsExists(); err != nil {
		log.Errorf("Failed to check IP rule [%v]: %v", ipRule, err)
		return err
	} else {
		if exists {
			log.Infof("IP rule [%v] is exists", ipRule)
		} else {
			log.Errorf("IP rule [%v] is NOT exists", ipRule)
		}
	}

	useKeeneticAPI := *cfg.General.UseKeeneticAPI
	var keeneticIfaces map[string]keenetic.Interface = nil
	if useKeeneticAPI {
		log.Infof("Usage of Keenetic API is enabled")
		var err error
		keeneticIfaces, err = keenetic.RciShowInterfaceMappedByIPNet()
		if err != nil {
			log.Errorf("Failed to query Keenetic API: %v", err)
			return err
		}
	} else {
		log.Warnf("Usage of Keenetic API is DISABLED. This may lead to wrong interface selection if you are using multiple interfaces for ipset")
	}

	if chosenIface, err := networking.ChooseBestInterface(ipset, useKeeneticAPI, keeneticIfaces); err != nil {
		log.Errorf("Failed to choose best interface: %v", err)
		return err
	} else {
		if chosenIface == nil {
			log.Errorf("Failed to choose best interface. All interfaces are down?")
		} else {
			log.Infof("Choosing interface %s", chosenIface.Attrs().Name)
		}

		if err := checkIpRoutes(ipset, chosenIface); err != nil {
			log.Errorf("Failed to check IP routes: %v", err)
			return err
		}
		if err := checkIpTables(ipset); err != nil {
			log.Errorf("Failed to check IP tables: %v", err)
			return err
		}
	}

	log.Infof("----------------- IPSet [%s] END ------------------", ipset.IpsetName)
	return nil
}

func checkIpTables(ipset *config.IpsetConfig) error {
	ipTableRules, err := networking.BuildIPTablesForIpset(ipset)
	if err != nil {
		log.Errorf("Failed to build IP tables: %v", err)
		return err
	}

	if existsMap, err := ipTableRules.CheckRulesExists(); err != nil {
		log.Errorf("Failed to check IP tables [%v]: %v", ipTableRules, err)
		return err
	} else {
		log.Infof("Checking IP tables rules presense")
		for rule, exists := range existsMap {
			if exists {
				log.Infof("IP tables rule [%v] is exists", *rule)
			} else {
				log.Errorf("IP tables rule [%v] is NOT exists", *rule)
			}
		}
	}

	return nil
}

func checkIpRoutes(ipset *config.IpsetConfig, chosenIface *networking.Interface) error {
	if routes, err := networking.ListRoutesInTable(ipset.Routing.IpRouteTable); err != nil {
		log.Errorf("Failed to list IP routes in table %d: %v", ipset.Routing.IpRouteTable, err)
		return err
	} else {
		log.Infof("There are %d IP routes in table %d:", len(routes), ipset.Routing.IpRouteTable)

		for idx, route := range routes {
			log.Infof("  %d. %v", idx+1, route)
		}

		requiredRoutes := 0
		if chosenIface != nil {
			requiredRoutes += 1
		}
		if ipset.Routing.KillSwitch {
			requiredRoutes += 1
		}

		if len(routes) < requiredRoutes {
			log.Errorf("Some of required IP routes are missing in table %d", ipset.Routing.IpRouteTable)
		} else if len(routes) > requiredRoutes {
			log.Warnf("Looks like there are some extra IP routes in table %d", ipset.Routing.IpRouteTable)
		} else {
			log.Infof("All required IP routes are present in table %d", ipset.Routing.IpRouteTable)
		}
	}

	if chosenIface != nil {
		defaultIpRoute := networking.BuildDefaultRoute(ipset.IpVersion, *chosenIface, ipset.Routing.IpRouteTable)
		if exists, err := defaultIpRoute.IsExists(); err != nil {
			log.Errorf("Failed to check default IP route [%v]: %v", defaultIpRoute, err)
			return err
		} else {
			if exists {
				log.Infof("Default IP route [%v] is exists", defaultIpRoute)
			} else {
				log.Errorf("Default IP route [%v] is NOT exists", defaultIpRoute)
			}
		}
	} else {
		log.Infof("Default IP route check SKIPPED because no interface is connected")
	}

	blackholeIpRoute := networking.BuildBlackholeRoute(ipset.IpVersion, ipset.Routing.IpRouteTable)
	if exists, err := blackholeIpRoute.IsExists(); err != nil {
		log.Errorf("Failed to check blackhole IP route [%v]: %v", blackholeIpRoute, err)
		return err
	} else {
		if exists {
			if ipset.Routing.KillSwitch {
				log.Infof("Blackhole IP route [%v] is exists", blackholeIpRoute)
			} else {
				log.Errorf("Blackhole IP route [%v] is EXISTS, but kill-switch is DISABLED", blackholeIpRoute)
			}
		} else {
			if ipset.Routing.KillSwitch {
				log.Errorf("Blackhole IP route [%v] is NOT exists, but kill-switch is ENABLED", blackholeIpRoute)
			} else {
				log.Infof("Blackhole IP route [%v] is not exists. This is OK because kill-switch is DISABLED.", blackholeIpRoute)
			}
		}
	}

	return nil
}
