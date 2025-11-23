package commands

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
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
	fs   *flag.FlagSet
	ctx  *AppContext
	cfg  *config.Config
	deps *domain.AppDependencies
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

	// Initialize dependencies
	g.deps = domain.NewDefaultDependencies()

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

	// Use unified component-based checking (same logic as API)
	hasFailures := false
	for _, ipset := range g.cfg.IPSets {
		if !g.checkIPSetComponents(ipset) {
			hasFailures = true
		}
	}

	if hasFailures {
		log.Errorf("Self-check completed with failures")
		return fmt.Errorf("self-check failed")
	}

	log.Infof("Self-check completed successfully")
	return nil
}

// checkIPSetComponents checks a single IPSet using the unified NetworkingComponent abstraction.
// This is the same logic used by the API's self-check endpoint.
// Returns true if all checks passed, false if any failed.
func (g *SelfCheckCommand) checkIPSetComponents(ipsetCfg *config.IPSetConfig) bool {
	log.Infof("----------------- IPSet [%s] ------------------", ipsetCfg.IPSetName)
	hasFailures := false

	// Get Keenetic client from dependencies
	var keeneticClient *keenetic.Client
	if g.deps != nil {
		if concreteClient, ok := g.deps.KeeneticClient().(*keenetic.Client); ok {
			keeneticClient = concreteClient
		}
	}

	// Build components using the unified ComponentBuilder
	builder := networking.NewComponentBuilder(keeneticClient)
	components, err := builder.BuildComponents(ipsetCfg)
	if err != nil {
		log.Errorf("Failed to build networking components: %v", err)
		return false
	}

	// Check each component using the unified abstraction
	for _, component := range components {
		exists, err := component.IsExists()
		shouldExist := component.ShouldExist()

		// State is OK if actual existence matches expected existence and no error occurred
		state := (exists == shouldExist) && err == nil

		var message string
		if err != nil {
			message = fmt.Sprintf("Error checking: %v", err)
			state = false
		} else {
			message = g.getComponentMessage(component, exists, shouldExist, ipsetCfg)
		}

		// Log the result
		if state {
			log.Infof("[%s] %s: %s", component.GetType(), component.GetDescription(), message)
		} else {
			log.Errorf("[%s] %s: %s", component.GetType(), component.GetDescription(), message)
			hasFailures = true
		}
	}

	log.Infof("----------------- IPSet [%s] END ------------------", ipsetCfg.IPSetName)
	return !hasFailures
}

// getComponentMessage generates an appropriate message based on component type and state.
// This mirrors the logic in api/check.go for consistency.
func (g *SelfCheckCommand) getComponentMessage(component networking.NetworkingComponent, exists bool, shouldExist bool, ipsetCfg *config.IPSetConfig) string {
	compType := component.GetType()

	switch compType {
	case networking.ComponentTypeIPSet:
		if exists && shouldExist {
			return fmt.Sprintf("IPSet [%s] exists", component.GetIPSetName())
		} else if !exists && shouldExist {
			return fmt.Sprintf("IPSet [%s] does NOT exist (missing)", component.GetIPSetName())
		} else if exists && !shouldExist {
			return fmt.Sprintf("IPSet [%s] exists but should NOT (unexpected)", component.GetIPSetName())
		}
		return fmt.Sprintf("IPSet [%s] not present", component.GetIPSetName())

	case networking.ComponentTypeIPRule:
		if exists && shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x lookup %d exists",
				ipsetCfg.Routing.FwMark, ipsetCfg.Routing.IPRouteTable)
		} else if !exists && shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x does NOT exist (missing)",
				ipsetCfg.Routing.FwMark)
		} else if exists && !shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x exists but should NOT (unexpected)",
				ipsetCfg.Routing.FwMark)
		}
		return fmt.Sprintf("IP rule with fwmark 0x%x not present", ipsetCfg.Routing.FwMark)

	case networking.ComponentTypeIPRoute:
		if routeComp, ok := component.(*networking.IPRouteComponent); ok {
			if routeComp.GetRouteType() == networking.RouteTypeBlackhole {
				if exists && shouldExist {
					return fmt.Sprintf("Blackhole route in table %d exists (kill-switch enabled)",
						ipsetCfg.Routing.IPRouteTable)
				} else if !exists && !shouldExist {
					return fmt.Sprintf("Blackhole route in table %d not present (kill-switch disabled)",
						ipsetCfg.Routing.IPRouteTable)
				} else if exists && !shouldExist {
					return fmt.Sprintf("Blackhole route in table %d exists but kill-switch is DISABLED (stale)",
						ipsetCfg.Routing.IPRouteTable)
				}
				return fmt.Sprintf("Blackhole route in table %d missing but kill-switch is ENABLED (missing)",
					ipsetCfg.Routing.IPRouteTable)
			} else {
				ifaceName := routeComp.GetInterfaceName()
				if exists && shouldExist {
					return fmt.Sprintf("Route in table %d via %s exists (active)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				} else if !exists && !shouldExist {
					return fmt.Sprintf("Route in table %d via %s not present (interface not best)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				} else if exists && !shouldExist {
					return fmt.Sprintf("Route in table %d via %s exists but is not best interface (stale)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				}
				return fmt.Sprintf("Route in table %d via %s missing but is best interface (missing)",
					ipsetCfg.Routing.IPRouteTable, ifaceName)
			}
		}

	case networking.ComponentTypeIPTables:
		if iptComp, ok := component.(*networking.IPTablesRuleComponent); ok {
			ruleDesc := iptComp.GetRuleDescription()
			if exists && shouldExist {
				return fmt.Sprintf("IPTables %s exists", ruleDesc)
			} else if !exists && shouldExist {
				return fmt.Sprintf("IPTables %s does NOT exist (missing)", ruleDesc)
			} else if exists && !shouldExist {
				return fmt.Sprintf("IPTables %s exists but should NOT (unexpected)", ruleDesc)
			}
			return fmt.Sprintf("IPTables %s not present", ruleDesc)
		}
	}

	// Fallback generic message
	if exists && shouldExist {
		return "Component exists as expected"
	} else if !exists && !shouldExist {
		return "Component absent as expected"
	} else if exists && !shouldExist {
		return "Component exists but should NOT (unexpected)"
	}
	return "Component missing but should exist"
}
