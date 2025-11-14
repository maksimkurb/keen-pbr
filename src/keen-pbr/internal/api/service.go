package api

import (
	"net/http"
	"strings"
)

// handleService handles service control endpoints
func (s *Server) handleService(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost && r.Method != http.MethodGet {
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
		return
	}

	action := strings.TrimPrefix(r.URL.Path, "/v1/service/")

	switch action {
	case "start":
		if err := s.service.Start(); err != nil {
			respondError(w, http.StatusInternalServerError, err.Error())
			return
		}
		respondJSON(w, http.StatusOK, map[string]string{"status": "started"})

	case "stop":
		if err := s.service.Stop(); err != nil {
			respondError(w, http.StatusInternalServerError, err.Error())
			return
		}
		respondJSON(w, http.StatusOK, map[string]string{"status": "stopped"})

	case "restart":
		if err := s.service.Restart(); err != nil {
			respondError(w, http.StatusInternalServerError, err.Error())
			return
		}
		respondJSON(w, http.StatusOK, map[string]string{"status": "restarted"})

	case "enable":
		s.service.Enable()
		respondJSON(w, http.StatusOK, map[string]string{"status": "enabled"})

	case "disable":
		s.service.Disable()
		respondJSON(w, http.StatusOK, map[string]string{"status": "disabled"})

	case "status":
		respondJSON(w, http.StatusOK, map[string]interface{}{
			"status":  s.service.GetStatus(),
			"enabled": s.service.IsEnabled(),
		})

	default:
		respondError(w, http.StatusNotFound, "Unknown action")
	}
}
