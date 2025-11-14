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
