package api

import (
	"encoding/json"
	"net/http"
	"os"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/core"
	"github.com/maksimkurb/keen-pbr/src/internal/dnsproxy"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ServiceManager is an interface for controlling the service lifecycle.
// This allows the API to control the service without importing the commands package directly.
type ServiceManager interface {
	Start() error
	Stop() error
	Restart() error
	IsRunning() bool
	GetAppliedConfigHash() string
	GetDependencies() *core.AppDependencies
}

// DNSCheckSubscriber is an interface for subscribing to DNS check events.
// Both DNSProxy and DNSCheckListener implement this interface.
type DNSCheckSubscriber interface {
	Subscribe() chan string
	Unsubscribe(ch chan string)
}

// DNSServersProvider provides access to the currently used DNS servers.
type DNSServersProvider interface {
	GetDNSStrings() []string
}

// Handler manages all API endpoints and dependencies.
type Handler struct {
	configPath   string
	deps         *core.AppDependencies
	serviceMgr   ServiceManager
	configHasher *config.ConfigHasher
	dnsProxy     *dnsproxy.DNSProxy
	// Tracking active check process (only one allowed at a time)
	activeCheckMu sync.Mutex
	activeProcess *os.Process
}

// NewHandler creates a new API handler with the given configuration path and dependencies.
func NewHandler(configPath string, deps *core.AppDependencies, serviceMgr ServiceManager, configHasher *config.ConfigHasher, dnsProxy *dnsproxy.DNSProxy) *Handler {
	return &Handler{
		configPath:   configPath,
		deps:         deps,
		serviceMgr:   serviceMgr,
		configHasher: configHasher,
		dnsProxy:     dnsProxy,
	}
}

// loadConfig loads the configuration from disk.
func (h *Handler) loadConfig() (*config.Config, error) {
	return config.LoadConfig(h.configPath)
}

// saveConfig saves the configuration to disk and resets the config hash cache.
func (h *Handler) saveConfig(cfg *config.Config) error {
	if err := cfg.WriteConfig(); err != nil {
		return err
	}

	// Reset the config hash cache after saving
	if _, err := h.configHasher.UpdateCurrentConfigHash(); err != nil {
		log.Warnf("Failed to update config hash after save: %v", err)
	}

	return nil
}

// validateConfig validates the configuration.
func (h *Handler) validateConfig(cfg *config.Config) error {
	return cfg.ValidateConfig()
}

// setActiveProcess sets the active check process and kills any existing one.
// This ensures only one check (ping/traceroute) runs at a time.
func (h *Handler) setActiveProcess(newProcess *os.Process) {
	h.activeCheckMu.Lock()
	defer h.activeCheckMu.Unlock()

	// Kill existing process if any
	if h.activeProcess != nil {
		_ = h.activeProcess.Kill()
		log.Debugf("Killed existing check process (PID: %d)", h.activeProcess.Pid)
	}

	h.activeProcess = newProcess
	if newProcess != nil {
		log.Debugf("Set active check process (PID: %d)", newProcess.Pid)
	}
}

// clearActiveProcess clears the active process reference.
func (h *Handler) clearActiveProcess(process *os.Process) {
	h.activeCheckMu.Lock()
	defer h.activeCheckMu.Unlock()

	// Only clear if it's the same process
	if h.activeProcess == process {
		log.Debugf("Killed cancelled check process (PID: %d)", h.activeProcess.Pid)
		h.activeProcess = nil
	}
}

// writeJSON writes a JSON response with the given status code and data.
func writeJSON(w http.ResponseWriter, statusCode int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	if err := json.NewEncoder(w).Encode(DataResponse{Data: data}); err != nil {
		log.Warnf("Failed to encode JSON response: %v", err)
	}
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
