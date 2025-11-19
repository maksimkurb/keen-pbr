package api

import (
	"fmt"
	"net/http"
	"os/exec"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

var (
	// Version information set via ldflags at build time
	Version = "dev"
	Date    = "n/a"
	Commit  = "n/a"
)

// hashCache holds cached config hash with expiration
type hashCache struct {
	hash      string
	timestamp time.Time
	mu        sync.RWMutex
}

var currentConfigHashCache = &hashCache{}

const hashCacheTTL = 5 * time.Minute

// getCachedConfigHash returns cached hash or recalculates if expired
func (h *Handler) getCachedConfigHash() (string, error) {
	currentConfigHashCache.mu.RLock()
	if time.Since(currentConfigHashCache.timestamp) < hashCacheTTL &&
		currentConfigHashCache.hash != "" {
		hash := currentConfigHashCache.hash
		currentConfigHashCache.mu.RUnlock()
		return hash, nil
	}
	currentConfigHashCache.mu.RUnlock()

	// Need to recalculate
	currentConfigHashCache.mu.Lock()
	defer currentConfigHashCache.mu.Unlock()

	// Double-check after acquiring write lock
	if time.Since(currentConfigHashCache.timestamp) < hashCacheTTL &&
		currentConfigHashCache.hash != "" {
		return currentConfigHashCache.hash, nil
	}

	// Load current config from file
	cfg, err := config.LoadConfig(h.configPath)
	if err != nil {
		return "", fmt.Errorf("failed to load config: %w", err)
	}

	// Calculate hash
	hasher := config.NewConfigHasher(cfg)
	hash, err := hasher.CalculateHash()
	if err != nil {
		return "", fmt.Errorf("failed to calculate hash: %w", err)
	}

	// Update cache
	currentConfigHashCache.hash = hash
	currentConfigHashCache.timestamp = time.Now()

	return hash, nil
}

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

	// Calculate current config hash (cached)
	currentHash, err := h.getCachedConfigHash()
	if err != nil {
		log.Warnf("Failed to get current config hash: %v", err)
		currentHash = "error"
	}
	response.CurrentConfigHash = currentHash

	// Get applied config hash from service
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
