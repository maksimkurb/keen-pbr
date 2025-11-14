package api

import (
	"encoding/json"
	"net/http"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// handleGeneralSettings handles general settings operations
func (s *Server) handleGeneralSettings(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		s.handleGetGeneralSettings(w, r)
	case http.MethodPut:
		s.handleUpdateGeneralSettings(w, r)
	default:
		respondError(w, http.StatusMethodNotAllowed, "Only GET and PUT methods are allowed")
	}
}

// handleGetGeneralSettings retrieves general settings
func (s *Server) handleGetGeneralSettings(w http.ResponseWriter, r *http.Request) {
	settings := s.config.GetGeneralSettings()
	respondJSON(w, http.StatusOK, settings)
}

// handleUpdateGeneralSettings updates general settings
func (s *Server) handleUpdateGeneralSettings(w http.ResponseWriter, r *http.Request) {
	var settings models.GeneralSettings
	if err := json.NewDecoder(r.Body).Decode(&settings); err != nil {
		respondError(w, http.StatusBadRequest, "Invalid request body: "+err.Error())
		return
	}

	if err := s.config.UpdateGeneralSettings(&settings); err != nil {
		respondError(w, http.StatusBadRequest, err.Error())
		return
	}

	// Save configuration
	if err := s.config.Save(s.configPath); err != nil {
		respondError(w, http.StatusInternalServerError, "Failed to save config: "+err.Error())
		return
	}

	respondJSON(w, http.StatusOK, settings)
}
