package networking

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ComponentBuilder builds all networking components for IPSet configurations.
// It provides a centralized way to generate all required network primitives
// (IPSets, IP rules, IP routes, IPTables rules) from configuration.
//
// The builder ensures components are created in the correct order:
//  1. IPSet - must exist before IPTables rules can reference it
//  2. IP Rule - routes marked packets to custom routing table
//  3. IPTables Rules - mark packets matching the ipset (only if configured)
//  4. IP Routes - define routes in custom table (per interface + blackhole)
//
// Usage:
//
//	builder := NewComponentBuilder(keeneticClient)
//	components, err := builder.BuildComponents(ipsetConfig)
//	for _, component := range components {
//	    if component.ShouldExist() {
//	        component.CreateIfNotExists()
//	    }
//	}
type ComponentBuilder struct {
	selector *InterfaceSelector
}

// NewComponentBuilder creates a new component builder with the given keenetic client.
// Pass nil for keeneticClient if Keenetic integration is not available.
func NewComponentBuilder(keeneticClient *keenetic.Client) *ComponentBuilder {
	selector := NewInterfaceSelector(keeneticClient)
	return &ComponentBuilder{
		selector: selector,
	}
}

// NewComponentBuilderWithSelector creates a builder with an existing selector
func NewComponentBuilderWithSelector(selector *InterfaceSelector) *ComponentBuilder {
	return &ComponentBuilder{
		selector: selector,
	}
}

// BuildComponents builds all networking components for a single IPSet configuration.
//
// The components are returned in the order they should be created:
// 1. IPSet - must exist before IPTables rules can reference it
// 2. IP Rule - routes marked packets to custom table
// 3. IPTables Rules - mark packets matching the ipset
// 4. IP Routes - define routes in the custom table (for all configured interfaces + blackhole)
//
// Returns an error if any component cannot be built.
func (b *ComponentBuilder) BuildComponents(cfg *config.IPSetConfig) ([]NetworkingComponent, error) {
	components := []NetworkingComponent{}

	// 1. IPSet component (must be first)
	components = append(components, NewIPSetComponent(cfg))

	// 2. IP Rule component
	components = append(components, NewIPRuleComponent(cfg))

	// 3. IPTables components (only if rules are configured)
	if len(cfg.IPTablesRules) > 0 {
		iptablesComponents, err := NewIPTablesRuleComponents(cfg)
		if err != nil {
			return nil, fmt.Errorf("failed to build iptables components for %s: %w", cfg.IPSetName, err)
		}
		for _, comp := range iptablesComponents {
			components = append(components, comp)
		}
	}

	// 4. IP Route components
	// Create route components for each configured interface
	for _, ifaceName := range cfg.Routing.Interfaces {
		routeComp, err := NewDefaultRouteComponentFromName(cfg, ifaceName, b.selector)
		if err != nil {
			// Interface doesn't exist yet, log warning but continue
			log.Warnf("Failed to create route component for interface %s: %v", ifaceName, err)
			continue
		}
		components = append(components, routeComp)
	}

	// Add blackhole route component
	components = append(components, NewBlackholeRouteComponent(cfg, b.selector))

	return components, nil
}

