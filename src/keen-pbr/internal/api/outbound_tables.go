package api

import (
	"io"
	"net/http"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// handleOutboundTables handles /v1/outbound-tables (list and create)
func (s *Server) handleOutboundTables(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		tables := s.config.GetAllOutboundTables()
		respondJSON(w, http.StatusOK, tables)

	case http.MethodPost:
		body, err := io.ReadAll(r.Body)
		if err != nil {
			respondError(w, http.StatusBadRequest, "Failed to read request body")
			return
		}

		table, err := models.UnmarshalOutboundTable(body)
		if err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body: "+err.Error())
			return
		}

		// Generate ID (in real implementation, extract from request or generate)
		id := generateID()

		if err := s.config.AddOutboundTable(id, table); err != nil {
			respondError(w, http.StatusBadRequest, err.Error())
			return
		}

		respondJSON(w, http.StatusCreated, map[string]interface{}{
			"id":    id,
			"table": table,
		})

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}

// handleOutboundTablesWithID handles /v1/outbound-tables/{id} (get, update, delete)
func (s *Server) handleOutboundTablesWithID(w http.ResponseWriter, r *http.Request) {
	id := extractID(r.URL.Path, "/v1/outbound-tables/")
	if id == "" {
		respondError(w, http.StatusBadRequest, "Outbound table ID is required")
		return
	}

	switch r.Method {
	case http.MethodGet:
		table, ok := s.config.GetOutboundTable(id)
		if !ok {
			respondError(w, http.StatusNotFound, "Outbound table not found")
			return
		}
		respondJSON(w, http.StatusOK, table)

	case http.MethodPut:
		body, err := io.ReadAll(r.Body)
		if err != nil {
			respondError(w, http.StatusBadRequest, "Failed to read request body")
			return
		}

		table, err := models.UnmarshalOutboundTable(body)
		if err != nil {
			respondError(w, http.StatusBadRequest, "Invalid request body: "+err.Error())
			return
		}

		if err := s.config.AddOutboundTable(id, table); err != nil {
			respondError(w, http.StatusBadRequest, err.Error())
			return
		}

		respondJSON(w, http.StatusOK, table)

	case http.MethodDelete:
		if !s.config.DeleteOutboundTable(id) {
			respondError(w, http.StatusNotFound, "Outbound table not found")
			return
		}
		respondJSON(w, http.StatusOK, map[string]string{"status": "deleted"})

	default:
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
	}
}
