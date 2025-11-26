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

	writeJSONData(w, SettingsResponse{General: cfg.General})
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
	cfg.General = &config.GeneralConfig{}

	// Apply partial updates (only update non-zero fields)
	cfg.General.ListsOutputDir = updates.ListsOutputDir
	cfg.General.InterfaceMonitoringIntervalSeconds = updates.InterfaceMonitoringIntervalSeconds

	// AutoUpdate settings
	cfg.General.AutoUpdate = &config.AutoUpdateConfig{}
	cfg.General.AutoUpdate.Enabled = updates.AutoUpdate.Enabled
	cfg.General.AutoUpdate.IntervalHours = updates.AutoUpdate.IntervalHours

	// DNS Server settings
	cfg.General.DNSServer = &config.DNSServerConfig{}
	cfg.General.DNSServer.Enable = updates.DNSServer.Enable
	cfg.General.DNSServer.ListenAddr = updates.DNSServer.ListenAddr
	cfg.General.DNSServer.ListenPort = updates.DNSServer.ListenPort
	cfg.General.DNSServer.Upstreams = updates.DNSServer.Upstreams
	cfg.General.DNSServer.CacheMaxDomains = updates.DNSServer.CacheMaxDomains
	cfg.General.DNSServer.DropAAAA = updates.DNSServer.DropAAAA
	cfg.General.DNSServer.IPSetEntryAdditionalTTLSec = updates.DNSServer.IPSetEntryAdditionalTTLSec
	cfg.General.DNSServer.Remap53Interfaces = updates.DNSServer.Remap53Interfaces

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

	writeJSONData(w, SettingsResponse{General: cfg.General})
}
