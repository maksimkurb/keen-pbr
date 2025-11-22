package api

import (
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

// CheckHealth performs health checks on the system.
// GET /api/v1/health
func (h *Handler) CheckHealth(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	response := HealthCheckResponse{
		Healthy: true,
		Checks:  make(map[string]CheckResult),
	}

	// Check configuration validity
	if err := h.validateConfig(cfg); err != nil {
		response.Healthy = false
		response.Checks["config_validation"] = CheckResult{
			Passed:  false,
			Message: "Configuration validation failed: " + err.Error(),
		}
	} else {
		response.Checks["config_validation"] = CheckResult{
			Passed:  true,
			Message: "Configuration is valid",
		}
	}

	// Check network configuration - verify interfaces exist
	interfaces, err := networking.GetInterfaceList()
	if err != nil {
		response.Healthy = false
		response.Checks["network_config"] = CheckResult{
			Passed:  false,
			Message: "Failed to get interface list: " + err.Error(),
		}
	} else {
		if err := networking.ValidateInterfacesArePresent(cfg, interfaces); err != nil {
			response.Healthy = false
			response.Checks["network_config"] = CheckResult{
				Passed:  false,
				Message: "Network configuration check failed: " + err.Error(),
			}
		} else {
			response.Checks["network_config"] = CheckResult{
				Passed:  true,
				Message: "Network configuration is valid",
			}
		}
	}

	// Check Keenetic connectivity
	if h.deps.KeeneticClient() != nil {
		if _, err := h.deps.KeeneticClient().GetVersion(); err != nil {
			response.Healthy = false
			response.Checks["keenetic_connectivity"] = CheckResult{
				Passed:  false,
				Message: "Failed to connect to Keenetic API: " + err.Error(),
			}
		} else {
			response.Checks["keenetic_connectivity"] = CheckResult{
				Passed:  true,
				Message: "Keenetic API is accessible",
			}
		}
	} else {
		response.Checks["keenetic_connectivity"] = CheckResult{
			Passed:  true,
			Message: "Keenetic integration disabled",
		}
	}

	writeJSONData(w, response)
}
