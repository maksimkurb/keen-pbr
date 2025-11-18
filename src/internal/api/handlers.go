package api

import (
	"encoding/json"
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
)

// ServiceManager is an interface for controlling the service lifecycle.
// This allows the API to control the service without importing the commands package directly.
type ServiceManager interface {
	Start() error
	Stop() error
	Restart() error
	IsRunning() bool
}

// Handler manages all API endpoints and dependencies.
type Handler struct {
	configPath string
	deps       *domain.AppDependencies
	serviceMgr ServiceManager
}

// NewHandler creates a new API handler with the given configuration path and dependencies.
func NewHandler(configPath string, deps *domain.AppDependencies, serviceMgr ServiceManager) *Handler {
	return &Handler{
		configPath: configPath,
		deps:       deps,
		serviceMgr: serviceMgr,
	}
}

// loadConfig loads the configuration from disk.
func (h *Handler) loadConfig() (*config.Config, error) {
	return config.LoadConfig(h.configPath)
}

// saveConfig saves the configuration to disk.
func (h *Handler) saveConfig(cfg *config.Config) error {
	return cfg.WriteConfig()
}

// validateConfig validates the configuration.
func (h *Handler) validateConfig(cfg *config.Config) error {
	return cfg.ValidateConfig()
}

// writeJSON writes a JSON response with the given status code and data.
func writeJSON(w http.ResponseWriter, statusCode int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(DataResponse{Data: data})
}

// writeJSONData writes a successful JSON response with data.
func writeJSONData(w http.ResponseWriter, data interface{}) {
	writeJSON(w, http.StatusOK, data)
}

// writeCreated writes a 201 Created response with data.
func writeCreated(w http.ResponseWriter, data interface{}) {
	writeJSON(w, http.StatusCreated, data)
}

// writeNoContent writes a 204 No Content response.
func writeNoContent(w http.ResponseWriter) {
	w.WriteHeader(http.StatusNoContent)
}

// decodeJSON decodes JSON from the request body.
func decodeJSON(r *http.Request, v interface{}) error {
	return json.NewDecoder(r.Body).Decode(v)
}
