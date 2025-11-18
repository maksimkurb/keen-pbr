package api

import (
	"net/http"
	"os"
	"os/exec"
)

// ControlService controls the keen-pbr service.
// POST /api/v1/service
func (h *Handler) ControlService(w http.ResponseWriter, r *http.Request) {
	var req ServiceControlRequest
	if err := decodeJSON(r, &req); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate state
	if req.State != "started" && req.State != "stopped" && req.State != "restarted" {
		WriteInvalidRequest(w, "Invalid state. Must be one of: started, stopped, restarted")
		return
	}

	scriptPath := "/opt/etc/init.d/S80keen-pbr"

	// Check if init script exists
	if _, err := os.Stat(scriptPath); err != nil {
		WriteServiceError(w, "Init script not found at "+scriptPath)
		return
	}

	var cmd *exec.Cmd
	var action string

	switch req.State {
	case "started":
		action = "start"
		cmd = exec.Command(scriptPath, "start")
	case "stopped":
		action = "stop"
		cmd = exec.Command(scriptPath, "stop")
	case "restarted":
		action = "restart"
		cmd = exec.Command(scriptPath, "restart")
	}

	// Run the command
	output, err := cmd.CombinedOutput()
	if err != nil {
		WriteServiceError(w, "Failed to "+action+" service: "+err.Error()+". Output: "+string(output))
		return
	}

	response := ServiceControlResponse{
		Status:  "success",
		Message: "Service " + action + " command executed successfully",
	}

	writeJSONData(w, response)
}

// ControlDnsmasq restarts the dnsmasq service.
// POST /api/v1/dnsmasq
func (h *Handler) ControlDnsmasq(w http.ResponseWriter, r *http.Request) {
	scriptPath := "/opt/etc/init.d/S56dnsmasq"

	// Check if init script exists
	if _, err := os.Stat(scriptPath); err != nil {
		WriteServiceError(w, "Init script not found at "+scriptPath)
		return
	}

	// Always restart dnsmasq
	cmd := exec.Command(scriptPath, "restart")

	// Run the command
	output, err := cmd.CombinedOutput()
	if err != nil {
		WriteServiceError(w, "Failed to restart dnsmasq: "+err.Error()+". Output: "+string(output))
		return
	}

	response := ServiceControlResponse{
		Status:  "success",
		Message: "dnsmasq restart command executed successfully",
	}

	writeJSONData(w, response)
}
