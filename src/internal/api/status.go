package api

import (
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"syscall"
)

var (
	// Version information set via ldflags at build time
	Version = "dev"
	Date    = "n/a"
	Commit  = "n/a"
)

// GetStatus returns system status information.
// GET /api/v1/status
func (h *Handler) GetStatus(w http.ResponseWriter, r *http.Request) {
	response := StatusResponse{
		Version: VersionInfo{
			Version: Version,
			Date:    Date,
			Commit:  Commit,
		},
		Services: make(map[string]ServiceInfo),
	}

	// Get Keenetic version if available (raw version string)
	if h.deps.KeeneticClient() != nil {
		if versionStr, err := h.deps.KeeneticClient().GetRawVersion(); err == nil {
			response.KeeneticVersion = versionStr
		}
	}

	// Check keen-pbr service status (check PID file, not init script)
	response.Services["keen-pbr"] = getKeenPbrServiceStatus()

	// Check dnsmasq service status
	response.Services["dnsmasq"] = getServiceStatus("dnsmasq", "/opt/etc/init.d/S56dnsmasq")

	writeJSONData(w, response)
}

// getKeenPbrServiceStatus checks if the keen-pbr service process is running
// by checking the PID file at /opt/var/run/keen-pbr.pid
func getKeenPbrServiceStatus() ServiceInfo {
	pidFile := "/opt/var/run/keen-pbr.pid"

	// Read PID file
	data, err := os.ReadFile(pidFile)
	if err != nil {
		return ServiceInfo{
			Status:  "stopped",
			Message: "Service is not running (no PID file)",
		}
	}

	// Parse PID
	pidStr := strings.TrimSpace(string(data))
	pid, err := strconv.Atoi(pidStr)
	if err != nil {
		return ServiceInfo{
			Status:  "unknown",
			Message: "Invalid PID file",
		}
	}

	// Check if process is running (send signal 0 to check without killing)
	process, err := os.FindProcess(pid)
	if err != nil {
		return ServiceInfo{
			Status:  "stopped",
			Message: "Process not found",
		}
	}

	// Try to send signal 0 (does nothing, just checks if process exists)
	err = process.Signal(syscall.Signal(0))
	if err != nil {
		return ServiceInfo{
			Status:  "stopped",
			Message: "Service is not running (stale PID file)",
		}
	}

	return ServiceInfo{
		Status:  "running",
		Message: "Service is running",
	}
}

// getServiceStatus checks if a service is running using init.d scripts.
func getServiceStatus(serviceName string, scriptPath string) ServiceInfo {
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
