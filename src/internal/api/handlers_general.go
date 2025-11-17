package api

import (
	"encoding/json"
	"fmt"
	"net/http"
)

// GeneralResponse represents general settings in API responses
type GeneralResponse struct {
	ListsOutputDir string `json:"lists_output_dir"`
	UseKeeneticDNS bool   `json:"use_keenetic_dns"`
	FallbackDNS    string `json:"fallback_dns"`
}

// GeneralRequest represents general settings in API requests
type GeneralRequest struct {
	ListsOutputDir *string `json:"lists_output_dir,omitempty"`
	UseKeeneticDNS *bool   `json:"use_keenetic_dns,omitempty"`
	FallbackDNS    *string `json:"fallback_dns,omitempty"`
}

// HandleGeneralGet returns general settings
func HandleGeneralGet(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		useKeeneticDNS := true
		if cfg.General.UseKeeneticDNS != nil {
			useKeeneticDNS = *cfg.General.UseKeeneticDNS
		}

		resp := GeneralResponse{
			ListsOutputDir: cfg.General.ListsOutputDir,
			UseKeeneticDNS: useKeeneticDNS,
			FallbackDNS:    cfg.General.FallbackDNS,
		}

		RespondOK(w, resp)
	}
}

// HandleGeneralUpdate updates general settings (partial update)
func HandleGeneralUpdate(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req GeneralRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		// Modify config
		err := ModifyConfig(configPath, func(cfg *config.Config) error {
			// Update only specified fields (partial update)
			if req.ListsOutputDir != nil {
				cfg.General.ListsOutputDir = *req.ListsOutputDir
			}
			if req.UseKeeneticDNS != nil {
				cfg.General.UseKeeneticDNS = req.UseKeeneticDNS
			}
			if req.FallbackDNS != nil {
				cfg.General.FallbackDNS = *req.FallbackDNS
			}
			return nil
		})

		if err != nil {
			RespondInternalError(w, err.Error())
			return
		}

		// Load updated config to return
		cfg, _ := LoadConfig(configPath)
		useKeeneticDNS := true
		if cfg.General.UseKeeneticDNS != nil {
			useKeeneticDNS = *cfg.General.UseKeeneticDNS
		}

		resp := GeneralResponse{
			ListsOutputDir: cfg.General.ListsOutputDir,
			UseKeeneticDNS: useKeeneticDNS,
			FallbackDNS:    cfg.General.FallbackDNS,
		}

		RespondOK(w, resp)
	}
}
