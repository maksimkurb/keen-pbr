package api

import (
	"encoding/json"
	"net/http"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// handleRules handles /v1/rules (list and create)
func (s *Server) handleRules(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		rules := s.config.GetAllRules()
		respondJSON(w, http.StatusOK, rules)

	case http.MethodPost:
		var rule models.Rule
		if err := json.NewDecoder(r.Body).Decode(&rule); err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body")
			return
		}

		// Generate ID if not provided
		id := rule.ID
		if id == "" {
			id = generateID()
		}

		if err := s.config.AddRule(id, &rule); err != nil {
			respondError(w, http.StatusBadRequest, err.Error())
			return
		}

		respondJSON(w, http.StatusCreated, rule)

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}

// handleRulesWithID handles /v1/rules/{id} (get, update, delete)
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

	case http.MethodPut:
		var rule models.Rule
		if err := json.NewDecoder(r.Body).Decode(&rule); err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body")
			return
		}

		if err := s.config.AddRule(id, &rule); err != nil {
			respondError(w, http.StatusBadRequest, err.Error())
			return
		}

		respondJSON(w, http.StatusOK, rule)

	case http.MethodDelete:
		if !s.config.DeleteRule(id) {
			respondError(w, http.StatusNotFound, "Rule not found")
			return
		}
		respondJSON(w, http.StatusOK, map[string]string{"status": "deleted"})

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
