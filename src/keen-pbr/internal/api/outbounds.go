package api

import (
	"net/http"
)

// handleOutboundsWithTag handles GET requests for specific outbound
func (s *Server) handleOutboundsWithTag(w http.ResponseWriter, r *http.Request) {
	tag := extractID(r.URL.Path, "/v1/outbounds/")
	if tag == "" {
		respondError(w, http.StatusBadRequest, "Outbound tag is required")
		return
	}

	switch r.Method {
	case http.MethodGet:
		outbound, exists := s.config.GetOutbound(tag)
		if !exists {
			respondError(w, http.StatusNotFound, "Outbound not found")
			return
		}
		respondJSON(w, http.StatusOK, outbound)

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
