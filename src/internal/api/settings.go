package api

import (
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// GetSettings returns general settings.
// GET /api/v1/settings
func (h *Handler) GetSettings(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Normalize settings to return proper defaults
	autoUpdate := cfg.General.IsAutoUpdateEnabled()                   // Get default if not set
	interfaceMonitoring := cfg.General.IsInterfaceMonitoringEnabled() // Get default if not set
	enableDNSProxy := cfg.General.IsDNSProxyEnabled()
	dropAAAA := cfg.General.IsDropAAAAEnabled()
	dnsProxyRemap53 := cfg.General.IsDNSProxyRemap53Enabled()

	response := config.GeneralConfig{
		ListsOutputDir: cfg.General.ListsOutputDir,
		UseKeeneticDNS: cfg.General.UseKeeneticDNS,
		FallbackDNS:    cfg.General.FallbackDNS,
		// APIBindAddress is excluded - not configurable via API
		AutoUpdateLists:           &autoUpdate,                          // Use helper to get default (true if nil)
		UpdateIntervalHours:       cfg.General.GetUpdateIntervalHours(), // Use helper to get default
		EnableInterfaceMonitoring: &interfaceMonitoring,                 // Use helper to get default (false if nil)

		// DNS Proxy settings - use getters to return defaults
		EnableDNSProxy:     &enableDNSProxy,
		DNSProxyListenAddr: cfg.General.GetDNSProxyListenAddr(),
		DNSProxyPort:       cfg.General.GetDNSProxyPort(),
		DNSUpstream:        cfg.General.GetDNSUpstream(),
		DNSCacheMaxDomains: cfg.General.GetDNSCacheMaxDomains(),
		DropAAAA:           &dropAAAA,
		TTLOverride:        int(cfg.General.GetTTLOverride()),
		DNSProxyInterfaces: cfg.General.GetDNSProxyInterfaces(),
		DNSProxyRemap53:    &dnsProxyRemap53,
	}

	writeJSONData(w, SettingsResponse{General: &response})
}

// UpdateSettings updates general settings (supports partial updates).
// PATCH /api/v1/settings
func (h *Handler) UpdateSettings(w http.ResponseWriter, r *http.Request) {
	var updates config.GeneralConfig
	if err := decodeJSON(r, &updates); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Ensure general config exists
	if cfg.General == nil {
		cfg.General = &config.GeneralConfig{}
	}

	// Apply partial updates (only update non-zero fields)
	if updates.ListsOutputDir != "" {
		cfg.General.ListsOutputDir = updates.ListsOutputDir
	}
	if updates.UseKeeneticDNS != nil {
		cfg.General.UseKeeneticDNS = updates.UseKeeneticDNS
	}
	if updates.FallbackDNS != "" {
		cfg.General.FallbackDNS = updates.FallbackDNS
	}
	if updates.AutoUpdateLists != nil {
		cfg.General.AutoUpdateLists = updates.AutoUpdateLists
	}
	if updates.UpdateIntervalHours > 0 {
		cfg.General.UpdateIntervalHours = updates.UpdateIntervalHours
	}
	if updates.EnableInterfaceMonitoring != nil {
		cfg.General.EnableInterfaceMonitoring = updates.EnableInterfaceMonitoring
	}

	// DNS Proxy settings
	if updates.EnableDNSProxy != nil {
		cfg.General.EnableDNSProxy = updates.EnableDNSProxy
	}
	if updates.DNSProxyListenAddr != "" {
		cfg.General.DNSProxyListenAddr = updates.DNSProxyListenAddr
	}
	if updates.DNSProxyPort > 0 {
		cfg.General.DNSProxyPort = updates.DNSProxyPort
	}
	if updates.DNSUpstream != nil {
		cfg.General.DNSUpstream = updates.DNSUpstream
	}
	if updates.DNSCacheMaxDomains > 0 {
		cfg.General.DNSCacheMaxDomains = updates.DNSCacheMaxDomains
	}
	if updates.DropAAAA != nil {
		cfg.General.DropAAAA = updates.DropAAAA
	}
	if updates.TTLOverride >= 0 {
		cfg.General.TTLOverride = updates.TTLOverride
	}
	if updates.DNSProxyInterfaces != nil {
		cfg.General.DNSProxyInterfaces = updates.DNSProxyInterfaces
	}
	if updates.DNSProxyRemap53 != nil {
		cfg.General.DNSProxyRemap53 = updates.DNSProxyRemap53
	}

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		WriteValidationError(w, "Configuration validation failed: "+err.Error(), nil)
		return
	}

	// Save configuration
	if err := h.saveConfig(cfg); err != nil {
		WriteInternalError(w, "Failed to save configuration: "+err.Error())
		return
	}

	// Normalize settings to return proper defaults
	autoUpdate := cfg.General.IsAutoUpdateEnabled()                   // Get default if not set
	interfaceMonitoring := cfg.General.IsInterfaceMonitoringEnabled() // Get default if not set
	enableDNSProxy := cfg.General.IsDNSProxyEnabled()
	dropAAAA := cfg.General.IsDropAAAAEnabled()
	dnsProxyRemap53 := cfg.General.IsDNSProxyRemap53Enabled()

	response := config.GeneralConfig{
		ListsOutputDir: cfg.General.ListsOutputDir,
		UseKeeneticDNS: cfg.General.UseKeeneticDNS,
		FallbackDNS:    cfg.General.FallbackDNS,
		// APIBindAddress is excluded - not configurable via API
		AutoUpdateLists:           &autoUpdate,                          // Use helper to get default (true if nil)
		UpdateIntervalHours:       cfg.General.GetUpdateIntervalHours(), // Use helper to get default
		EnableInterfaceMonitoring: &interfaceMonitoring,                 // Use helper to get default (false if nil)

		// DNS Proxy settings - use getters to return defaults
		EnableDNSProxy:     &enableDNSProxy,
		DNSProxyListenAddr: cfg.General.GetDNSProxyListenAddr(),
		DNSProxyPort:       cfg.General.GetDNSProxyPort(),
		DNSUpstream:        cfg.General.GetDNSUpstream(),
		DNSCacheMaxDomains: cfg.General.GetDNSCacheMaxDomains(),
		DropAAAA:           &dropAAAA,
		TTLOverride:        int(cfg.General.GetTTLOverride()),
		DNSProxyInterfaces: cfg.General.GetDNSProxyInterfaces(),
		DNSProxyRemap53:    &dnsProxyRemap53,
	}

	writeJSONData(w, SettingsResponse{General: &response})
}
