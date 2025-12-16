package api

import (
	"net/http"

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

	// Get DNS servers from the running DNS proxy
	if h.dnsProxy != nil {
		response.DNSServers = h.dnsProxy.GetDNSStrings()
	}

	// Get current config hash (cached) from ConfigHasher
	currentHash, err := h.configHasher.GetCurrentConfigHash()
	if err != nil {
		log.Warnf("Failed to get current config hash: %v", err)
		currentHash = "error"
	}
	response.CurrentConfigHash = currentHash

	// Check keen-pbr service status using ServiceManager
	keenPbrInfo := h.getKeenPbrServiceStatus()

	// Get active config hash from service
	activeHash := h.serviceMgr.GetAppliedConfigHash()
	keenPbrInfo.ConfigHash = activeHash

	// Compare current config hash with keen-pbr service active hash
	response.ConfigurationOutdated = (currentHash != "" &&
		activeHash != "" &&
		currentHash != activeHash &&
		currentHash != "error")

	response.Services["keen-pbr"] = keenPbrInfo

	// Add IPSet status information (only if service is running)
	if h.serviceMgr.IsRunning() {
		response.IPSets = h.getIPSetStatuses()
	}

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

// getIPSetStatuses returns status information for all configured IPSets.
// Returns nil if NetworkManager has no active state (service not running or no routing applied).
func (h *Handler) getIPSetStatuses() map[string]IPSetStatusInfo {
	// Get active interfaces from NetworkManager (shared dependency)
	activeInterfaces := h.deps.NetworkManager().GetActiveInterfaces()

	// If no active interfaces tracked, return nil (service likely not running)
	if len(activeInterfaces) == 0 {
		return nil
	}

	// Build status info map for each IPSet that has active state
	statuses := make(map[string]IPSetStatusInfo, len(activeInterfaces))
	for ipsetName, activeIface := range activeInterfaces {
		var ifacePtr *string
		isBlackhole := false

		if activeIface == "" {
			// Empty string means blackhole route
			isBlackhole = true
			// ifacePtr remains nil
		} else {
			// Active interface exists
			ifacePtr = &activeIface
		}

		statuses[ipsetName] = IPSetStatusInfo{
			ActiveInterface: ifacePtr,
			IsBlackhole:     isBlackhole,
		}
	}

	return statuses
}
