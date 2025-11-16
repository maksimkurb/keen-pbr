package commands

import (
	"encoding/binary"
	"flag"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		g.cfg = cfg
	}

	if err := networking.ValidateInterfacesArePresent(g.cfg, ctx.Interfaces); err != nil {
		log.Errorf("Configuration validation failed: %v", err)
		networking.PrintMissingInterfacesHelp()
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
		if err := binary.Write(os.Stdout, binary.LittleEndian, cfg.Bytes()); err != nil {
			log.Errorf("Failed to output config: %v", err)
			return err
		}
	}

	log.Infof("----------------- Configuration END ------------------")

	for _, ipset := range g.cfg.IPSets {
		if err := checkIpset(g.cfg, ipset); err != nil {
			log.Errorf("Failed to check ipset routing configuration [%s]: %v", ipset.IPSetName, err)
			return err
		}
	}

	log.Infof("Self-check completed successfully")
	return nil
}

func checkIpset(cfg *config.Config, ipsetCfg *config.IPSetConfig) error {
	log.Infof("----------------- IPSet [%s] ------------------", ipsetCfg.IPSetName)
	log.Infof("Blackhole route will be automatically added when all interfaces are down")

	ipset := networking.BuildIPSet(ipsetCfg.IPSetName, ipsetCfg.IPVersion)

	if exists, err := ipset.IsExists(); err != nil {
		log.Errorf("Failed to check ipset presense [%s]: %v", ipsetCfg.IPSetName, err)
		return err
	} else {
		if exists {
			log.Infof("ipset [%s] is exists", ipsetCfg.IPSetName)
		} else {
			log.Errorf("ipset [%s] is NOT exists", ipsetCfg.IPSetName)
		}
	}

	ipRule := networking.BuildIPRuleForIpset(ipsetCfg)
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

	if chosenIface, err := networking.ChooseBestInterface(ipsetCfg, useKeeneticAPI, keeneticIfaces); err != nil {
		log.Errorf("Failed to choose best interface: %v", err)
		return err
	} else {
		if chosenIface == nil {
			log.Errorf("Failed to choose best interface. All interfaces are down?")
		} else {
			log.Infof("Choosing interface %s", chosenIface.Attrs().Name)
		}

		if err := checkIpRoutes(ipsetCfg, chosenIface); err != nil {
			log.Errorf("Failed to check IP routes: %v", err)
			return err
		}
		if err := checkIpTables(ipsetCfg); err != nil {
			log.Errorf("Failed to check iptable rules: %v", err)
			return err
		}
	}

	log.Infof("----------------- IPSet [%s] END ------------------", ipsetCfg.IPSetName)
	return nil
}

func checkIpTables(ipset *config.IPSetConfig) error {
	ipTableRules, err := networking.BuildIPTablesForIpset(ipset)
	if err != nil {
		log.Errorf("Failed to build iptable rules: %v", err)
		return err
	}

	if existsMap, err := ipTableRules.CheckRulesExists(); err != nil {
		log.Errorf("Failed to check iptable rules [%v]: %v", ipTableRules, err)
		return err
	} else {
		log.Infof("Checking iptable rules presense")
		for rule, exists := range existsMap {
			if exists {
				log.Infof("iptable rule [%v] is exists", *rule)
			} else {
				log.Errorf("iptable rule [%v] is NOT exists", *rule)
			}
		}
	}

	return nil
}

func checkIpRoutes(ipset *config.IPSetConfig, chosenIface *networking.Interface) error {
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
			requiredRoutes += 1 // default route
		} else {
			requiredRoutes += 1 // blackhole route when no interface is available
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
		defaultIpRoute := networking.BuildDefaultRoute(ipset.IPVersion, *chosenIface, ipset.Routing.IpRouteTable)
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

	blackholeIpRoute := networking.BuildBlackholeRoute(ipset.IPVersion, ipset.Routing.IpRouteTable)
	if exists, err := blackholeIpRoute.IsExists(); err != nil {
		log.Errorf("Failed to check blackhole IP route [%v]: %v", blackholeIpRoute, err)
		return err
	} else {
		if exists {
			if chosenIface == nil {
				log.Infof("Blackhole IP route [%v] is exists (all interfaces are down)", blackholeIpRoute)
			} else {
				log.Errorf("Blackhole IP route [%v] EXISTS, but interface is UP (should not be present)", blackholeIpRoute)
			}
		} else {
			if chosenIface == nil {
				log.Errorf("Blackhole IP route [%v] is NOT exists, but all interfaces are DOWN (should be present)", blackholeIpRoute)
			} else {
				log.Infof("Blackhole IP route [%v] is not exists. This is OK because interface is UP.", blackholeIpRoute)
			}
		}
	}

	return nil
}
