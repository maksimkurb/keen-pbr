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

	scriptPath := "/opt/etc/init.d/S80keen-pbr"

	// Check if init script exists
	if _, err := os.Stat(scriptPath); err != nil {
		WriteServiceError(w, "Init script not found at "+scriptPath)
		return
	}

	var cmd *exec.Cmd
	var action string

	if req.Up {
		action = "start"
		cmd = exec.Command(scriptPath, "start")
	} else {
		action = "stop"
		cmd = exec.Command(scriptPath, "stop")
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
