package api

import (
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/lists"
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

	// Load config to resolve DNS servers
	cfg, err := h.loadConfig()
	if err == nil {
		// Get DNS servers using centralized resolution method
		dnsServers := lists.ResolveDNSServers(cfg, h.deps.KeeneticClient())

		// Convert to API types
		if len(dnsServers) > 0 {
			apiDNSServers := make([]DNSServerInfo, len(dnsServers))
			for i, server := range dnsServers {
				apiDNSServers[i] = DNSServerInfo{
					Type:     string(server.Type),
					Endpoint: server.Endpoint,
					Port:     server.Port,
					Domain:   server.Domain,
				}
			}
			response.DNSServers = apiDNSServers
		}
	} else {
		log.Warnf("Failed to load config for DNS servers: %v", err)
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

