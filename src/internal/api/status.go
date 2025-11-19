package api

import (
	"net/http"
	"os/exec"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
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

	// Get current config hash (cached) from ConfigHasher
	currentHash, err := h.configHasher.GetCurrentConfigHash()
	if err != nil {
		log.Warnf("Failed to get current config hash: %v", err)
		currentHash = "error"
	}
	response.CurrentConfigHash = currentHash

	// Get active config hash from ConfigHasher (via ServiceManager)
	appliedHash := h.serviceMgr.GetAppliedConfigHash()
	response.AppliedConfigHash = appliedHash

	// Compare hashes to determine if config is outdated
	response.ConfigurationOutdated = (currentHash != "" &&
		appliedHash != "" &&
		currentHash != appliedHash &&
		currentHash != "error")

	// Check keen-pbr service status using ServiceManager
	keenPbrInfo := h.getKeenPbrServiceStatus()
	keenPbrInfo.ConfigHash = appliedHash
	response.Services["keen-pbr"] = keenPbrInfo

	// Check dnsmasq service status
	response.Services["dnsmasq"] = getServiceStatus("dnsmasq", "/opt/etc/init.d/S56dnsmasq")

	writeJSONData(w, response)
}

// getKeenPbrServiceStatus checks if the keen-pbr service is running
// using the ServiceManager
func (h *Handler) getKeenPbrServiceStatus() ServiceInfo {
	if h.serviceMgr.IsRunning() {
		return ServiceInfo{
			Status:  "running",
			Message: "Service is running",
		}
	}

	return ServiceInfo{
		Status:  "stopped",
		Message: "Service is not running",
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
