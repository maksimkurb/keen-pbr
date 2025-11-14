package singbox

import (
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/config"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// GenerateConfig generates a sing-box configuration from the application config
func GenerateConfig(cfg *config.Config) (*Config, error) {
	outbounds := cfg.GetAllOutbounds()
	rules := cfg.GetAllRules()
	generalSettings := cfg.GetGeneralSettings()

	// Sort rules by priority (higher priority first)
	sortedRules := make([]*models.Rule, 0, len(rules))
	for _, rule := range rules {
		if rule.Enabled {
			sortedRules = append(sortedRules, rule)
		}
	}
	sort.Slice(sortedRules, func(i, j int) bool {
		return sortedRules[i].Priority > sortedRules[j].Priority
	})

	singboxConfig := &Config{
		Log: LogConfig{
			Disabled:  false,
			Level:     "warn",
			Timestamp: false,
		},
		DNS:         generateDNSConfig(sortedRules, outbounds, generalSettings),
		NTP:         make(map[string]any),
		Certificate: make(map[string]any),
		Endpoints:   []any{},
		Inbounds:    generateInbounds(),
		Outbounds:   generateOutbounds(outbounds),
		Route:       generateRouteConfig(sortedRules, outbounds),
		Services:    []any{},
		Experimental: ExperimentalConfig{
			CacheFile: CacheFileConfig{
				Enabled:     true,
				Path:        "/tmp/sing-box/cache.db",
				StoreFakeIP: true,
			},
			ClashAPI: ClashAPIConfig{
				ExternalController: "0.0.0.0:9090",
				ExternalUI:         "ui",
			},
		},
	}

	return singboxConfig, nil
}

// convertModelsDNSToSingbox converts a models.DNS to a singbox DNSServer
func convertModelsDNSToSingbox(dns *models.DNS, tag string, detour string, domainResolver string) DNSServer {
	server := DNSServer{
		Type:       string(dns.Type),
		Tag:        tag,
		Server:     dns.Server,
		ServerPort: int(dns.Port),
	}

	// Set path for HTTPS DNS (DoH)
	if dns.Type == models.DNSTypeHTTPS && dns.Path != "" {
		server.Path = dns.Path
	}

	if detour != "" {
		server.Detour = detour
	}
	if domainResolver != "" {
		server.DomainResolver = domainResolver
	}

	return server
}

// generateDNSConfig generates DNS configuration
func generateDNSConfig(rules []*models.Rule, outbounds map[string]models.Outbound, generalSettings *models.GeneralSettings) DNSConfig {
	dnsServers := []DNSServer{}

	// Add bootstrap DNS server from general settings or use default
	if generalSettings != nil && generalSettings.BootstrapDNSServer != nil {
		bootstrapDNS := convertModelsDNSToSingbox(generalSettings.BootstrapDNSServer, "bootstrap-dns-server", "", "")
		dnsServers = append(dnsServers, bootstrapDNS)
	} else {
		// Default bootstrap DNS server
		dnsServers = append(dnsServers, DNSServer{
			Type:       "udp",
			Tag:        "bootstrap-dns-server",
			Server:     "8.8.8.8",
			ServerPort: 53,
		})
	}

	// Add default DNS server from general settings or use default
	if generalSettings != nil && generalSettings.DefaultDNSServer != nil {
		defaultDNS := convertModelsDNSToSingbox(generalSettings.DefaultDNSServer, "dns-server", "", "bootstrap-dns-server")
		dnsServers = append(dnsServers, defaultDNS)
	} else {
		// Default DNS server (DoH)
		dnsServers = append(dnsServers, DNSServer{
			Type:           "https",
			Tag:            "dns-server",
			Server:         "dns.google",
			ServerPort:     443,
			Path:           "/dns-query",
			DomainResolver: "bootstrap-dns-server",
		})
	}

	// Add fakeip server
	dnsServers = append(dnsServers, DNSServer{
		Type:       "fakeip",
		Tag:        "fakeip-server",
		Inet4Range: "198.18.0.0/15",
	})

	// Add domain resolvers for each outbound
	for tag, outbound := range outbounds {
		if iface, ok := outbound.(*models.InterfaceOutbound); ok {
			// Use bootstrap DNS for domain resolvers
			var bootstrapServer string
			var bootstrapPort int
			if generalSettings != nil && generalSettings.BootstrapDNSServer != nil {
				bootstrapServer = generalSettings.BootstrapDNSServer.Server
				bootstrapPort = int(generalSettings.BootstrapDNSServer.Port)
			} else {
				bootstrapServer = "8.8.8.8"
				bootstrapPort = 53
			}

			dnsServers = append(dnsServers, DNSServer{
				Type:           "udp",
				Tag:            fmt.Sprintf("%s-domain-resolver", tag),
				Server:         bootstrapServer,
				ServerPort:     bootstrapPort,
				Detour:         tag,
				DomainResolver: "bootstrap-dns-server",
			})
			_ = iface // use variable to avoid unused warning
		}
	}

	// Add custom DNS servers from rules
	ruleCustomDNSMap := make(map[string]bool) // Track unique custom DNS servers
	for _, rule := range rules {
		if !rule.Enabled {
			continue
		}

		for i, customDNS := range rule.CustomDNSServers {
			dnsTag := fmt.Sprintf("rule-%s-dns-%d", rule.ID, i)
			if !ruleCustomDNSMap[dnsTag] {
				var detour string
				if customDNS.ThroughOutbound {
					// Determine which outbound to use for this DNS
					switch table := rule.OutboundTable.(type) {
					case *models.StaticOutboundTable:
						detour = table.Outbound
					case *models.URLTestOutboundTable:
						if len(table.Outbounds) > 0 {
							detour = table.Outbounds[0]
						}
					}
				}

				customDNSServer := convertModelsDNSToSingbox(&customDNS, dnsTag, detour, "bootstrap-dns-server")
				dnsServers = append(dnsServers, customDNSServer)
				ruleCustomDNSMap[dnsTag] = true
			}
		}
	}

	dnsRules := []DNSRule{
		{
			Action:    "reject",
			QueryType: "HTTPS",
		},
		{
			Action:       "reject",
			DomainSuffix: "use-application-dns.net",
		},
	}

	// Generate DNS rules from application rules
	for _, rule := range rules {
		if !rule.Enabled {
			continue
		}

		ruleSetTags := generateRuleSetTags(rule)
		if len(ruleSetTags) > 0 {
			// Determine which DNS server to use for this rule
			dnsServer := "fakeip-server"
			if len(rule.CustomDNSServers) > 0 {
				// Use the first custom DNS server for this rule
				dnsServer = fmt.Sprintf("rule-%s-dns-0", rule.ID)
			}

			dnsRules = append(dnsRules, DNSRule{
				Action:     "route",
				Server:     dnsServer,
				RewriteTTL: 60,
				RuleSet:    ruleSetTags,
			})
		}
	}

	return DNSConfig{
		Servers:          dnsServers,
		Rules:            dnsRules,
		Final:            "dns-server",
		Strategy:         "ipv4_only",
		IndependentCache: true,
	}
}

// generateInbounds generates inbound configurations
func generateInbounds() []Inbound {
	return []Inbound{
		{
			Type:        "tproxy",
			Tag:         "tproxy-in",
			Listen:      "127.0.0.1",
			ListenPort:  1602,
			TCPFastOpen: true,
			UDPFragment: true,
		},
		{
			Type:       "direct",
			Tag:        "dns-in",
			Listen:     "127.0.0.42",
			ListenPort: 53,
		},
	}
}

// generateOutbounds generates outbound configurations
func generateOutbounds(outbounds map[string]models.Outbound) []interface{} {
	result := []interface{}{
		map[string]interface{}{
			"type": "direct",
			"tag":  "direct-out",
		},
	}

	// Convert application outbounds to sing-box outbounds
	for tag, outbound := range outbounds {
		switch ob := outbound.(type) {
		case *models.InterfaceOutbound:
			result = append(result, map[string]interface{}{
				"type":            "direct",
				"tag":             tag,
				"bind_interface":  ob.IfName,
				"domain_resolver": fmt.Sprintf("%s-domain-resolver", tag),
			})
		case *models.ProxyOutbound:
			// Parse proxy URL and generate proper sing-box outbound
			proxyOutbound, err := ParseProxyURL(tag, ob.URL)
			if err != nil {
				// If parsing fails, create a direct outbound as fallback
				result = append(result, map[string]interface{}{
					"type": "direct",
					"tag":  tag,
				})
			} else {
				result = append(result, proxyOutbound)
			}
		}
	}

	return result
}

// generateRouteConfig generates routing configuration
func generateRouteConfig(rules []*models.Rule, outbounds map[string]models.Outbound) RouteConfig {
	routeRules := []RouteRule{
		{
			Action:  "sniff",
			Inbound: []string{"tproxy-in", "dns-in"},
		},
		{
			Action:   "hijack-dns",
			Protocol: "dns",
		},
	}

	ruleSets := []RuleSet{}

	// Generate route rules from application rules
	for _, rule := range rules {
		if !rule.Enabled {
			continue
		}

		// Get outbound tag(s) from the rule's outbound table
		var outboundTag string
		switch table := rule.OutboundTable.(type) {
		case *models.StaticOutboundTable:
			outboundTag = table.Outbound
		case *models.URLTestOutboundTable:
			if len(table.Outbounds) > 0 {
				outboundTag = table.Outbounds[0] // Use first outbound for now
			}
		}

		if outboundTag == "" {
			continue
		}

		// Generate rule sets for this rule
		ruleSetTags := generateRuleSetTags(rule)
		if len(ruleSetTags) > 0 {
			// Add route rule
			var ruleSetValue any
			if len(ruleSetTags) == 1 {
				ruleSetValue = ruleSetTags[0]
			} else {
				ruleSetValue = ruleSetTags
			}

			routeRules = append(routeRules, RouteRule{
				Action:  "route",
				Inbound: "tproxy-in",
				Outbound: outboundTag,
				RuleSet: ruleSetValue,
			})

			// Add rule sets
			for i, list := range rule.Lists {
				ruleSet := convertListToRuleSet(rule.ID, list, i)
				if ruleSet != nil {
					ruleSets = append(ruleSets, *ruleSet)
				}
			}
		}
	}

	return RouteConfig{
		Rules:                 routeRules,
		RuleSet:               ruleSets,
		Final:                 "direct-out",
		AutoDetectInterface:   true,
		DefaultDomainResolver: "dns-server",
	}
}

// generateRuleSetTags generates rule set tags for a rule
func generateRuleSetTags(rule *models.Rule) []string {
	var tags []string
	for i, list := range rule.Lists {
		tag := generateRuleSetTag(rule.ID, list, i)
		if tag != "" {
			tags = append(tags, tag)
		}
	}
	return tags
}

// generateRuleSetTag generates a rule set tag for a list
func generateRuleSetTag(ruleID string, list models.List, index int) string {
	// Sanitize rule ID to make it a valid tag
	sanitizedID := strings.ReplaceAll(ruleID, " ", "-")
	sanitizedID = strings.ToLower(sanitizedID)

	switch list.GetType() {
	case models.ListTypeLocal, models.ListTypeRemote:
		return fmt.Sprintf("%s-list-%d-ruleset", sanitizedID, index)
	case models.ListTypeInline:
		// Inline lists are not supported in rule sets, skip them
		return ""
	default:
		return ""
	}
}

// convertListToRuleSet converts a List to a RuleSet
func convertListToRuleSet(ruleID string, list models.List, index int) *RuleSet {
	switch l := list.(type) {
	case *models.LocalList:
		tag := generateRuleSetTag(ruleID, l, index)
		return &RuleSet{
			Type:   "local",
			Tag:    tag,
			Format: string(l.Format),
			Path:   l.Path,
		}
	case *models.RemoteList:
		tag := generateRuleSetTag(ruleID, l, index)
		return &RuleSet{
			Type:           "remote",
			Tag:            tag,
			Format:         string(l.Format),
			URL:            l.URL,
			UpdateInterval: formatDuration(l.UpdateInterval),
		}
	case *models.InlineList:
		// Inline lists need to be converted to local files first
		// For now, we'll skip them
		return nil
	default:
		return nil
	}
}

// formatDuration formats a duration to sing-box format (e.g., "1d", "1h")
func formatDuration(d interface{}) string {
	// sing-box expects duration in format like "1d", "1h", "30m", etc.
	// For time.Duration, we'll convert to the most appropriate unit
	if duration, ok := d.(time.Duration); ok {
		hours := int(duration.Hours())
		if hours >= 24 && hours%24 == 0 {
			return fmt.Sprintf("%dd", hours/24)
		}
		if hours > 0 {
			return fmt.Sprintf("%dh", hours)
		}
		minutes := int(duration.Minutes())
		if minutes > 0 {
			return fmt.Sprintf("%dm", minutes)
		}
		seconds := int(duration.Seconds())
		return fmt.Sprintf("%ds", seconds)
	}

	// Default to 1 day if type is unknown
	return "1d"
}
