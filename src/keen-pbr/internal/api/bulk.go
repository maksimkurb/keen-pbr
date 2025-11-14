package api

import (
	"encoding/json"
	"fmt"
	"net/http"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// handleOutboundsBulk handles /v1/outbounds (GET and PUT for bulk operations)
func (s *Server) handleOutboundsBulk(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		outbounds := s.config.GetAllOutbounds()
		respondJSON(w, http.StatusOK, outbounds)

	case http.MethodPut:
		var outbounds []json.RawMessage
		if err := json.NewDecoder(r.Body).Decode(&outbounds); err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body: "+err.Error())
			return
		}

		// Unmarshal outbounds
		parsedOutbounds := make([]models.Outbound, 0, len(outbounds))
		for i, rawOutbound := range outbounds {
			outbound, err := models.UnmarshalOutbound(rawOutbound)
			if err != nil {
				respondError(w, http.StatusBadRequest, fmt.Sprintf("Failed to unmarshal outbound at index %d: %v", i, err))
				return
			}
			parsedOutbounds = append(parsedOutbounds, outbound)
		}

		// Replace all outbounds
		if err := s.config.ReplaceAllOutbounds(parsedOutbounds); err != nil {
			respondError(w, http.StatusBadRequest, "Failed to replace outbounds: "+err.Error())
			return
		}

		// Save configuration
		if err := s.config.Save(s.configPath); err != nil {
			respondError(w, http.StatusInternalServerError, "Failed to save configuration: "+err.Error())
			return
		}

		respondJSON(w, http.StatusOK, map[string]string{"status": "success"})

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}

// handleRulesBulk handles /v1/rules (GET and PUT for bulk operations)
func (s *Server) handleRulesBulk(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		rules := s.config.GetAllRules()
		respondJSON(w, http.StatusOK, rules)

	case http.MethodPut:
		var rules []*models.Rule
		if err := json.NewDecoder(r.Body).Decode(&rules); err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body: "+err.Error())
			return
		}

		// Build map of available outbound tags
		allOutbounds := s.config.GetAllOutbounds()
		availableOutbounds := make(map[string]bool, len(allOutbounds))
		for tag := range allOutbounds {
			availableOutbounds[tag] = true
		}

		// Replace all rules (this will validate that all referenced outbounds exist)
		if err := s.config.ReplaceAllRules(rules, availableOutbounds); err != nil {
			respondError(w, http.StatusBadRequest, "Failed to replace rules: "+err.Error())
			return
		}

		// Save configuration
		if err := s.config.Save(s.configPath); err != nil {
			respondError(w, http.StatusInternalServerError, "Failed to save configuration: "+err.Error())
			return
		}

		respondJSON(w, http.StatusOK, map[string]string{"status": "success"})

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
