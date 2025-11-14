package api

import (
	"net/http"
)

// handleRulesWithID handles /v1/rules/{id} (get only)
func (s *Server) handleRulesWithID(w http.ResponseWriter, r *http.Request) {
	id := extractID(r.URL.Path, "/v1/rules/")
	if id == "" {
		respondError(w, http.StatusBadRequest, "Rule ID is required")
		return
	}

	switch r.Method {
	case http.MethodGet:
		rule, ok := s.config.GetRule(id)
		if !ok {
			respondError(w, http.StatusNotFound, "Rule not found")
			return
		}
		respondJSON(w, http.StatusOK, rule)

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
