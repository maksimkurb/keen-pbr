package networking

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// DNSRedirectConfig contains the configuration needed to build DNS redirect components.
// This is a subset of the full DNS proxy configuration.
type DNSRedirectConfig struct {
	// Enabled indicates whether DNS redirection should be active
	Enabled bool
	// ListenAddr is the address the DNS proxy listens on (e.g., "[::]" for dual-stack)
	ListenAddr string
	// ListenPort is the port the DNS proxy listens on
	ListenPort uint16
}

// GlobalConfig contains configuration for global (service-level) components.
type GlobalConfig struct {
	DNSRedirect DNSRedirectConfig
}

// GlobalConfigFromAppConfig creates a GlobalConfig from the application config.
func GlobalConfigFromAppConfig(cfg *config.Config) GlobalConfig {
	return GlobalConfig{
		DNSRedirect: DNSRedirectConfig{
			Enabled:    cfg.General.IsDNSProxyEnabled(),
			ListenAddr: cfg.General.GetDNSProxyListenAddr(),
			ListenPort: uint16(cfg.General.GetDNSProxyPort()),
		},
	}
}

// GlobalComponentBuilder builds global/service-level networking components.
// These are components that are not tied to a specific IPSet configuration,
// but rather to the overall service configuration.
//
// Currently includes:
//   - DNSRedirectComponent: iptables rules for DNS traffic redirection
//
// Usage:
//
//	builder := NewGlobalComponentBuilder()
//	components, err := builder.BuildComponents(globalConfig)
//	for _, component := range components {
//	    if component.ShouldExist() {
//	        component.CreateIfNotExists()
//	    }
//	}
type GlobalComponentBuilder struct{}

// NewGlobalComponentBuilder creates a new global component builder.
func NewGlobalComponentBuilder() *GlobalComponentBuilder {
	return &GlobalComponentBuilder{}
}

// BuildComponents builds all global networking components based on the configuration.
// Returns components in the order they should be created.
func (b *GlobalComponentBuilder) BuildComponents(cfg GlobalConfig) ([]NetworkingComponent, error) {
	var components []NetworkingComponent

	// DNS Redirect component (only if DNS proxy is enabled)
	if cfg.DNSRedirect.Enabled {
		dnsRedirect, err := NewDNSRedirectComponent(cfg.DNSRedirect.ListenAddr, cfg.DNSRedirect.ListenPort)
		if err != nil {
			return nil, err
		}
		components = append(components, dnsRedirect)
	}

	return components, nil
}
