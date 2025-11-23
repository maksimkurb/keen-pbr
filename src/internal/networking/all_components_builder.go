package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// AllComponentsBuilder composes both per-ipset and global component builders
// to provide a unified way to build all networking components.
//
// This builder is the single source of truth for:
//   - Applying configuration at startup
//   - Re-applying on SIGHUP (config reload)
//   - Re-applying on SIGUSR1 (interface changes)
//   - Rollback/undo on shutdown
//   - Self-check (CLI and API)
//
// Usage:
//
//	builder := NewAllComponentsBuilder(keeneticClient)
//	components, err := builder.BuildAllComponents(appConfig, globalConfig)
//	for _, component := range components {
//	    if component.ShouldExist() {
//	        component.CreateIfNotExists()
//	    }
//	}
type AllComponentsBuilder struct {
	perIPSetBuilder *ComponentBuilder
	globalBuilder   *GlobalComponentBuilder
}

// NewAllComponentsBuilder creates a new composite component builder.
// Pass nil for keeneticClient if Keenetic integration is not available.
func NewAllComponentsBuilder(keeneticClient InterfaceLister) *AllComponentsBuilder {
	return &AllComponentsBuilder{
		perIPSetBuilder: NewComponentBuilder(keeneticClient),
		globalBuilder:   NewGlobalComponentBuilder(),
	}
}

// NewAllComponentsBuilderWithSelector creates a builder with an existing interface selector.
func NewAllComponentsBuilderWithSelector(selector *InterfaceSelector) *AllComponentsBuilder {
	return &AllComponentsBuilder{
		perIPSetBuilder: NewComponentBuilderWithSelector(selector),
		globalBuilder:   NewGlobalComponentBuilder(),
	}
}

// BuildAllComponents builds ALL networking components from the configuration.
// This includes both per-ipset components and global service-level components.
//
// The components are returned in the order they should be created:
// 1. Global components (DNS redirect, etc.)
// 2. Per-IPSet components (for each ipset: IPSet, IP Rule, IPTables Rules, IP Routes)
//
// Returns an error if any component cannot be built.
func (b *AllComponentsBuilder) BuildAllComponents(appCfg *config.Config, globalCfg GlobalConfig) ([]NetworkingComponent, error) {
	var allComponents []NetworkingComponent

	// 1. Build global components
	globalComponents, err := b.globalBuilder.BuildComponents(globalCfg)
	if err != nil {
		return nil, err
	}
	allComponents = append(allComponents, globalComponents...)

	// 2. Build per-ipset components
	for _, ipset := range appCfg.IPSets {
		ipsetComponents, err := b.perIPSetBuilder.BuildComponents(ipset)
		if err != nil {
			return nil, err
		}
		allComponents = append(allComponents, ipsetComponents...)
	}

	return allComponents, nil
}

// BuildGlobalComponents builds only global/service-level components.
// This is useful when only global components need to be applied or checked.
func (b *AllComponentsBuilder) BuildGlobalComponents(globalCfg GlobalConfig) ([]NetworkingComponent, error) {
	return b.globalBuilder.BuildComponents(globalCfg)
}

// BuildPersistentComponents builds persistent components (iptables, ip rules, ipsets, dns redirect).
// These are components that should remain active regardless of interface state.
//
// Includes:
//   - DNS redirect component (global)
//   - IPSet components
//   - IP Rule components
//   - IPTables Rule components
//
// Excludes:
//   - IP Route components (handled by BuildRoutingComponents)
func (b *AllComponentsBuilder) BuildPersistentComponents(appCfg *config.Config, globalCfg GlobalConfig) ([]NetworkingComponent, error) {
	var persistent []NetworkingComponent

	// 1. Build global components (DNS redirect is persistent)
	globalComponents, err := b.globalBuilder.BuildComponents(globalCfg)
	if err != nil {
		return nil, err
	}
	persistent = append(persistent, globalComponents...)

	// 2. Build per-ipset components, filtering to persistent types only
	for _, ipset := range appCfg.IPSets {
		ipsetComponents, err := b.perIPSetBuilder.BuildComponents(ipset)
		if err != nil {
			return nil, err
		}

		for _, component := range ipsetComponents {
			compType := component.GetType()
			// Only include persistent components
			if compType == ComponentTypeIPSet ||
				compType == ComponentTypeIPRule ||
				compType == ComponentTypeIPTables {
				persistent = append(persistent, component)
			}
		}
	}

	return persistent, nil
}

// BuildRoutingComponents builds only routing components (ip routes).
// These are dynamic components that adapt to interface up/down events.
//
// Includes:
//   - IP Route components (both default and blackhole)
func (b *AllComponentsBuilder) BuildRoutingComponents(appCfg *config.Config) ([]NetworkingComponent, error) {
	var routing []NetworkingComponent

	for _, ipset := range appCfg.IPSets {
		ipsetComponents, err := b.perIPSetBuilder.BuildComponents(ipset)
		if err != nil {
			return nil, err
		}

		for _, component := range ipsetComponents {
			if component.GetType() == ComponentTypeIPRoute {
				routing = append(routing, component)
			}
		}
	}

	return routing, nil
}

// BuildIPSetComponents builds only the per-ipset components for a single IPSet.
// This is a convenience method that delegates to the underlying ComponentBuilder.
func (b *AllComponentsBuilder) BuildIPSetComponents(ipsetCfg *config.IPSetConfig) ([]NetworkingComponent, error) {
	return b.perIPSetBuilder.BuildComponents(ipsetCfg)
}
