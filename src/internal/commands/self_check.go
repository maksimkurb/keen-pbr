package commands

import (
	"encoding/binary"
	"flag"
	"os"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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
		if err := checkIpset(ipset); err != nil {
			log.Errorf("Failed to check ipset routing configuration [%s]: %v", ipset.IPSetName, err)
			return err
		}
	}

	log.Infof("Self-check completed successfully")
	return nil
}

func checkIpset(ipsetCfg *config.IPSetConfig) error {
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

	ipRule := networking.NewIPRuleBuilder(ipsetCfg).Build()
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

	// Create Keenetic client and interface selector
	client := keenetic.NewClient(nil)
	selector := networking.NewInterfaceSelector(client)

	if chosenIface, ifaceIndex, err := selector.ChooseBest(ipsetCfg); err != nil {
		log.Errorf("Failed to choose best interface: %v", err)
		return err
	} else {
		if chosenIface == nil {
			log.Errorf("Failed to choose best interface. All interfaces are down?")
		} else {
			log.Infof("Choosing interface %s", chosenIface.Attrs().Name)
		}

		if err := checkIPRoutes(ipsetCfg, chosenIface, ifaceIndex); err != nil {
			log.Errorf("Failed to check IP routes: %v", err)
			return err
		}
		if err := checkIPTables(ipsetCfg); err != nil {
			log.Errorf("Failed to check iptable rules: %v", err)
			return err
		}
	}

	log.Infof("----------------- IPSet [%s] END ------------------", ipsetCfg.IPSetName)
	return nil
}

func checkIPTables(ipset *config.IPSetConfig) error {
	ipTableRules, err := networking.NewIPTablesBuilder(ipset).Build()
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

func checkIPRoutes(ipset *config.IPSetConfig, chosenIface *networking.Interface, ifaceIndex int) error {
	if routes, err := networking.ListRoutesInTable(ipset.Routing.IPRouteTable); err != nil {
		log.Errorf("Failed to list IP routes in table %d: %v", ipset.Routing.IPRouteTable, err)
		return err
	} else {
		log.Infof("There are %d IP routes in table %d:", len(routes), ipset.Routing.IPRouteTable)

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
			log.Errorf("Some of required IP routes are missing in table %d", ipset.Routing.IPRouteTable)
		} else if len(routes) > requiredRoutes {
			log.Warnf("Looks like there are some extra IP routes in table %d", ipset.Routing.IPRouteTable)
		} else {
			log.Infof("All required IP routes are present in table %d", ipset.Routing.IPRouteTable)
		}
	}

	if chosenIface != nil {
		defaultIPRoute := networking.BuildDefaultRoute(ipset.IPVersion, *chosenIface, ipset.Routing.IPRouteTable, ifaceIndex)
		if exists, err := defaultIPRoute.IsExists(); err != nil {
			log.Errorf("Failed to check default IP route [%v]: %v", defaultIPRoute, err)
			return err
		} else {
			if exists {
				log.Infof("Default IP route [%v] is exists", defaultIPRoute)
			} else {
				log.Errorf("Default IP route [%v] is NOT exists", defaultIPRoute)
			}
		}
	} else {
		log.Infof("Default IP route check SKIPPED because no interface is connected")
	}

	blackholeIPRoute := networking.BuildBlackholeRoute(ipset.IPVersion, ipset.Routing.IPRouteTable)
	if exists, err := blackholeIPRoute.IsExists(); err != nil {
		log.Errorf("Failed to check blackhole IP route [%v]: %v", blackholeIPRoute, err)
		return err
	} else {
		if exists {
			if chosenIface == nil {
				log.Infof("Blackhole IP route [%v] is exists (all interfaces are down)", blackholeIPRoute)
			} else {
				log.Errorf("Blackhole IP route [%v] EXISTS, but interface is UP (should not be present)", blackholeIPRoute)
			}
		} else {
			if chosenIface == nil {
				log.Errorf("Blackhole IP route [%v] is NOT exists, but all interfaces are DOWN (should be present)", blackholeIPRoute)
			} else {
				log.Infof("Blackhole IP route [%v] is not exists. This is OK because interface is UP.", blackholeIPRoute)
			}
		}
	}

	return nil
}
