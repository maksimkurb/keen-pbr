package api

import "github.com/maksimkurb/keen-pbr/src/internal/config"

// DataResponse wraps successful responses with a "data" field.
type DataResponse struct {
	Data interface{} `json:"data"`
}

// ListsResponse returns all lists in the configuration.
type ListsResponse struct {
	Lists []*config.ListSource `json:"lists"`
}

// IPSetsResponse returns all ipsets in the configuration.
type IPSetsResponse struct {
	IPSets []*config.IPSetConfig `json:"ipsets"`
}

// SettingsResponse returns general settings.
type SettingsResponse struct {
	General *config.GeneralConfig `json:"general"`
}

// StatusResponse returns system status information.
type StatusResponse struct {
	Version         string                 `json:"version"`
	KeeneticVersion string                 `json:"keenetic_version,omitempty"`
	Services        map[string]ServiceInfo `json:"services"`
}

// ServiceInfo contains information about a service.
type ServiceInfo struct {
	Status  string `json:"status"`  // "running", "stopped", "unknown"
	Message string `json:"message,omitempty"`
}

// ServiceControlRequest controls the keen-pbr service.
type ServiceControlRequest struct {
	Up bool `json:"up"`
}

// ServiceControlResponse returns the result of service control operation.
type ServiceControlResponse struct {
	Status  string `json:"status"`
	Message string `json:"message,omitempty"`
}

// HealthCheckRequest contains parameters for health checks.
type HealthCheckRequest struct {
	IPSetName string `json:"ipset_name,omitempty"`
	Domain    string `json:"domain,omitempty"`
}

// HealthCheckResponse returns health check results.
type HealthCheckResponse struct {
	Healthy bool                   `json:"healthy"`
	Checks  map[string]CheckResult `json:"checks"`
}

// CheckResult contains the result of a single health check.
type CheckResult struct {
	Passed  bool   `json:"passed"`
	Message string `json:"message,omitempty"`
}
