package commands

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
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

	// Check global components (DNS redirect, etc.)
	if !g.checkGlobalComponents() {
		hasFailures = true
	}

	// Check per-ipset components
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

// checkGlobalComponents checks global/service-level components (DNS redirect, etc.).
// Returns true if all checks passed, false if any failed.
func (g *SelfCheckCommand) checkGlobalComponents() bool {
	log.Infof("----------------- Global Components ------------------")
	hasFailures := false

	// Build global config and components
	globalCfg := networking.GlobalConfigFromAppConfig(g.cfg)
	builder := networking.NewGlobalComponentBuilder()
	components, err := builder.BuildComponents(globalCfg)
	if err != nil {
		log.Errorf("Failed to build global components: %v", err)
		return false
	}

	if len(components) == 0 {
		log.Infof("No global components configured")
		log.Infof("----------------- Global Components END --------------")
		return true
	}

	// Check each component
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
			message = g.getStatusMessage(exists, shouldExist)
		}

		// Log the result
		if state {
			log.Infof("[%s] %s: %s", component.GetType(), component.GetDescription(), message)
		} else {
			log.Errorf("[%s] %s: %s", component.GetType(), component.GetDescription(), message)
			hasFailures = true
		}
	}

	log.Infof("----------------- Global Components END --------------")
	return !hasFailures
}

// checkIPSetComponents checks a single IPSet using the unified NetworkingComponent abstraction.
// This is the same logic used by the API's self-check endpoint.
// Returns true if all checks passed, false if any failed.
func (g *SelfCheckCommand) checkIPSetComponents(ipsetCfg *config.IPSetConfig) bool {
	log.Infof("----------------- IPSet [%s] ------------------", ipsetCfg.IPSetName)
	hasFailures := false

	// Get Keenetic client from dependencies
	var keeneticClient domain.KeeneticClient
	if g.deps != nil {
		keeneticClient = g.deps.KeeneticClient()
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
			message = g.getStatusMessage(exists, shouldExist)
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

// getStatusMessage generates a generic status message based on existence.
func (g *SelfCheckCommand) getStatusMessage(exists bool, shouldExist bool) string {
	if exists && shouldExist {
		return "OK"
	} else if !exists && shouldExist {
		return "Missing"
	} else if exists && !shouldExist {
		return "Unexpected"
	}
	return "Not present"
}
