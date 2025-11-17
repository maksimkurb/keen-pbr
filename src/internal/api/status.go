package api

import (
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"strings"
)

// GetStatus returns system status information.
// GET /api/v1/status
func (h *Handler) GetStatus(w http.ResponseWriter, r *http.Request) {
	response := StatusResponse{
		Version:  getVersion(),
		Services: make(map[string]ServiceInfo),
	}

	// Get Keenetic version if available
	if h.deps.KeeneticClient() != nil {
		if version, err := h.deps.KeeneticClient().GetVersion(); err == nil {
			response.KeeneticVersion = fmt.Sprintf("KeeneticOS %d.%d", version.Major, version.Minor)
		}
	}

	// Check keen-pbr service status
	response.Services["keen-pbr"] = getServiceStatus("keen-pbr")

	// Check dnsmasq service status
	response.Services["dnsmasq"] = getServiceStatus("dnsmasq")

	writeJSONData(w, response)
}

// getVersion reads the version from the VERSION file.
func getVersion() string {
	data, err := os.ReadFile("VERSION")
	if err != nil {
		return "unknown"
	}
	return strings.TrimSpace(string(data))
}

// getServiceStatus checks if a service is running using init.d scripts.
func getServiceStatus(serviceName string) ServiceInfo {
	// Try to check service status using init.d script
	scriptPath := "/opt/etc/init.d/S80" + serviceName

	// Check if init script exists
	if _, err := os.Stat(scriptPath); err != nil {
		return ServiceInfo{
			Status:  "unknown",
			Message: "Init script not found",
		}
	}

	// Run status check
	cmd := exec.Command(scriptPath, "check")
	err := cmd.Run()

	if err == nil {
		return ServiceInfo{
			Status:  "running",
			Message: "Service is running",
		}
	}

	// If check command failed, service is likely stopped
	if exitErr, ok := err.(*exec.ExitError); ok {
		if exitErr.ExitCode() == 1 {
			return ServiceInfo{
				Status:  "stopped",
				Message: "Service is not running",
			}
		}
	}

	return ServiceInfo{
		Status:  "unknown",
		Message: "Unable to determine service status",
	}
}
