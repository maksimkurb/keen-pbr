package lists

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// KeeneticClientInterface defines minimal interface for DNS server retrieval
// This avoids circular dependency with domain package
type KeeneticClientInterface interface {
	GetDNSServers() ([]keenetic.DnsServerInfo, error)
}

// ResolveDNSServers resolves the list of DNS servers to use based on configuration.
//
// Logic:
// - If use_keenetic_dns is enabled:
//   - Try to fetch DNS servers from Keenetic
//   - If failed or list is empty, use fallback DNS
//   - If fallback DNS is also empty, return empty list
//
// - If use_keenetic_dns is disabled:
//   - Use fallback DNS
//   - If fallback DNS is empty, return empty list
func ResolveDNSServers(cfg *config.Config, keeneticClient KeeneticClientInterface) []keenetic.DnsServerInfo {
	if cfg.General.UseKeeneticDNS == nil || !*cfg.General.UseKeeneticDNS {
		// use_keenetic_dns is disabled or not set, use fallback
		return useFallbackDNS(cfg)
	}

	// use_keenetic_dns is enabled, try to fetch from Keenetic
	if keeneticClient == nil {
		log.Warnf("Keenetic DNS enabled but client not available, using fallback DNS")
		return useFallbackDNS(cfg)
	}

	servers, err := keeneticClient.GetDNSServers()
	if err != nil {
		log.Warnf("Failed to fetch Keenetic DNS servers: %v, using fallback DNS", err)
		return useFallbackDNS(cfg)
	}

	if len(servers) == 0 {
		log.Warnf("Keenetic returned empty DNS server list, using fallback DNS")
		return useFallbackDNS(cfg)
	}

	log.Infof("Fetched %d Keenetic DNS server(s): %v", len(servers), servers)
	return servers
}

// useFallbackDNS returns a DNS server list from the fallback DNS configuration.
// If fallback DNS is empty, returns an empty list.
func useFallbackDNS(cfg *config.Config) []keenetic.DnsServerInfo {
	if cfg.General.FallbackDNS == "" {
		log.Debugf("No fallback DNS configured, using empty DNS server list")
		return []keenetic.DnsServerInfo{}
	}

	log.Infof("Using fallback DNS: %s", cfg.General.FallbackDNS)
	return []keenetic.DnsServerInfo{
		{
			Type:     keenetic.DnsServerTypePlain,
			Proxy:    cfg.General.FallbackDNS,
			Endpoint: cfg.General.FallbackDNS,
			Domain:   nil,
			Port:     "",
		},
	}
}
