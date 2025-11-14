package api

import (
	"net/http"
)

// handleConfig handles /v1/config (get and update full config)
func (s *Server) handleConfig(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		data, err := s.config.ToJSON()
		if err != nil {
			respondError(w, http.StatusInternalServerError, "Failed to export config")
			return
		}

		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write(data)

	case http.MethodPut:
		// TODO: Implement full config update
		// This would require parsing the entire config and replacing it
		respondError(w, http.StatusNotImplemented, "Full config update not yet implemented")

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
