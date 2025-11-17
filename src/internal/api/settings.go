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
