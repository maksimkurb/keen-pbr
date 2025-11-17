package api

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"
	"strings"
)

// ServiceControlRequest represents service control request
type ServiceControlRequest struct {
	Up bool `json:"up"`
}

// ServiceControlResponse represents service control response
type ServiceControlResponse struct {
	Status  string `json:"status"`
	Message string `json:"message"`
}

// HandleServiceControl starts or stops the keen-pbr service
func HandleServiceControl(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req ServiceControlRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		var cmd *exec.Cmd
		var action string

		if req.Up {
			cmd = exec.Command("/opt/etc/init.d/S80keen-pbr", "start")
			action = "start"
		} else {
			cmd = exec.Command("/opt/etc/init.d/S80keen-pbr", "stop")
			action = "stop"
		}

		out, err := cmd.CombinedOutput()
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to %s service: %v - %s", action, err, string(out)))
			return
		}

		// Verify status
		checkCmd := exec.Command("/opt/etc/init.d/S80keen-pbr", "check")
		checkOut, _ := checkCmd.CombinedOutput()
		status := "unknown"
		if strings.Contains(string(checkOut), "alive") || strings.Contains(string(checkOut), "running") {
			status = "running"
		} else {
			status = "stopped"
		}

		message := fmt.Sprintf("Service %sed successfully", action)
		if req.Up && status != "running" {
			message = "Service start command executed, but status is " + status
		} else if !req.Up && status != "stopped" {
			message = "Service stop command executed, but status is " + status
		}

		resp := ServiceControlResponse{
			Status:  status,
			Message: message,
		}

		RespondOK(w, resp)
	}
}
