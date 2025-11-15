package api

import (
	"net/http"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/singbox"
)

// handleSingboxConfig handles sing-box config generation
func (s *Server) handleSingboxConfig(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		respondError(w, http.StatusMethodNotAllowed, "Only GET method is allowed")
		return
	}

	// Generate sing-box config from application config
	singboxConfig, err := singbox.GenerateConfig(s.config)
	if err != nil {
		respondError(w, http.StatusInternalServerError, "Failed to generate sing-box config: "+err.Error())
		return
	}

	// Return the generated config as JSON
	respondJSON(w, http.StatusOK, singboxConfig)
}

// handleSingboxDownload handles sing-box binary download/update
func (s *Server) handleSingboxDownload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		respondError(w, http.StatusMethodNotAllowed, "Only POST method is allowed")
		return
	}

	// Get configuration
	generalSettings := s.config.GetGeneralSettings()

	// Create downloader config
	downloaderConfig := singbox.DownloaderConfig{
		Version:     singbox.DefaultVersion,
		InstallPath: singbox.DefaultInstallPath,
	}

	// Override with settings if provided
	if generalSettings != nil {
		if generalSettings.SingBoxVersion != "" {
			downloaderConfig.Version = generalSettings.SingBoxVersion
		}
		if generalSettings.SingBoxPath != "" {
			downloaderConfig.InstallPath = generalSettings.SingBoxPath
		}
	}

	// Create downloader
	downloader := singbox.NewDownloader(downloaderConfig)

	// Download and install
	if err := downloader.Download(); err != nil {
		respondError(w, http.StatusInternalServerError, "Failed to download sing-box: "+err.Error())
		return
	}

	respondJSON(w, http.StatusOK, map[string]string{
		"status":  "success",
		"version": downloaderConfig.Version,
		"path":    downloaderConfig.InstallPath,
	})
}

// handleSingboxVersion handles getting sing-box version info
func (s *Server) handleSingboxVersion(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		respondError(w, http.StatusMethodNotAllowed, "Only GET method is allowed")
		return
	}

	// Get configuration
	generalSettings := s.config.GetGeneralSettings()

	// Determine paths and versions
	configuredVersion := singbox.DefaultVersion
	installPath := singbox.DefaultInstallPath

	if generalSettings != nil {
		if generalSettings.SingBoxVersion != "" {
			configuredVersion = generalSettings.SingBoxVersion
		}
		if generalSettings.SingBoxPath != "" {
			installPath = generalSettings.SingBoxPath
		}
	}

	// Create downloader to check status
	downloader := singbox.NewDownloader(singbox.DownloaderConfig{
		Version:     configuredVersion,
		InstallPath: installPath,
	})

	// Get detailed status
	status := downloader.GetStatus()

	respondJSON(w, http.StatusOK, status)
}
