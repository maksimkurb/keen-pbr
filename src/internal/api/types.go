package api

import "github.com/maksimkurb/keen-pbr/src/internal/config"

// DataResponse wraps successful responses with a "data" field.
type DataResponse struct {
	Data interface{} `json:"data"`
}

// ListInfo contains list information with statistics.
type ListInfo struct {
	ListName string          `json:"list_name"`
	Type     string          `json:"type"` // "url", "file", "hosts"
	URL      string          `json:"url,omitempty"`
	File     string          `json:"file,omitempty"`
	Stats    *ListStatistics `json:"stats"` // nil if statistics not yet calculated
}

// ListStatistics contains statistics about list content and download status.
// Count fields (TotalHosts, IPv4Subnets, IPv6Subnets) are pointers and will be
// null in JSON if statistics haven't been calculated yet.
// Download status (Downloaded, LastModified) is always available for URL-based lists.
type ListStatistics struct {
	TotalHosts   *int    `json:"total_hosts"`              // null if not yet calculated
	IPv4Subnets  *int    `json:"ipv4_subnets"`             // null if not yet calculated
	IPv6Subnets  *int    `json:"ipv6_subnets"`             // null if not yet calculated
	Downloaded   bool    `json:"downloaded,omitempty"`     // Only for URL-based lists
	LastModified *string `json:"last_modified,omitempty"`  // RFC3339 format, only for URL-based lists
}

// ListsResponse returns all lists in the configuration.
type ListsResponse struct {
	Lists []*ListInfo `json:"lists"`
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
	Version         VersionInfo            `json:"version"`
	KeeneticVersion string                 `json:"keenetic_version,omitempty"`
	Services        map[string]ServiceInfo `json:"services"`
}

// VersionInfo contains build version information.
type VersionInfo struct {
	Version string `json:"version"`
	Date    string `json:"date"`
	Commit  string `json:"commit"`
}

// ServiceInfo contains information about a service.
type ServiceInfo struct {
	Status  string `json:"status"`  // "running", "stopped", "unknown"
	Message string `json:"message,omitempty"`
}

// ServiceControlRequest controls the keen-pbr service.
type ServiceControlRequest struct {
	State string `json:"state"` // "started", "stopped", "restarted"
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
