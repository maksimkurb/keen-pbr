package api

import (
	"encoding/json"
	"net/http"
)

// DataResponse wraps response data in the standard format
type DataResponse struct {
	Data interface{} `json:"data"`
}

// RespondJSON sends a JSON response wrapped in the standard format
func RespondJSON(w http.ResponseWriter, statusCode int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(DataResponse{Data: data})
}

// RespondOK sends a 200 OK response with data
func RespondOK(w http.ResponseWriter, data interface{}) {
	RespondJSON(w, http.StatusOK, data)
}

// RespondCreated sends a 201 Created response with data
func RespondCreated(w http.ResponseWriter, data interface{}) {
	RespondJSON(w, http.StatusCreated, data)
}

// RespondNoContent sends a 204 No Content response
func RespondNoContent(w http.ResponseWriter) {
	w.WriteHeader(http.StatusNoContent)
}
