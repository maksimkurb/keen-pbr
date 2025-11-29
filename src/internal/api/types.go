package api

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

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
	Hosts    []string        `json:"hosts,omitempty"` // Only included for GET single list of type "hosts"
	Stats    *ListStatistics `json:"stats"`           // nil if statistics not yet calculated
}

// ListStatistics contains statistics about list content and download status.
// Count fields (TotalHosts, IPv4Subnets, IPv6Subnets) are pointers and will be
// null in JSON if statistics haven't been calculated yet.
// Download status (Downloaded, LastModified) is always available for URL-based lists.
type ListStatistics struct {
	TotalHosts   *int    `json:"total_hosts"`             // null if not yet calculated
	IPv4Subnets  *int    `json:"ipv4_subnets"`            // null if not yet calculated
	IPv6Subnets  *int    `json:"ipv6_subnets"`            // null if not yet calculated
	Downloaded   bool    `json:"downloaded,omitempty"`    // Only for URL-based lists
	LastModified *string `json:"last_modified,omitempty"` // RFC3339 format, only for URL-based lists
}

// ListsResponse returns all lists in the configuration.
type ListsResponse struct {
	Lists []*ListInfo `json:"lists"`
}

// ListDownloadResponse returns the result of downloading a list.
type ListDownloadResponse struct {
	*ListInfo
	Changed bool `json:"changed"` // true if the list was updated, false if unchanged
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
	Version               VersionInfo            `json:"version"`
	KeeneticVersion       string                 `json:"keenetic_version"`
	Services              map[string]ServiceInfo `json:"services"`
	CurrentConfigHash     string                 `json:"current_config_hash"`
	ConfigurationOutdated bool                   `json:"configuration_outdated"`
	DNSServers            []string               `json:"dns_servers"` // Upstream DNS servers
}

// DNSServerInfo contains information about a DNS server.
// Uses the shared type from service package.
type DNSServerInfo = service.DNSServerInfo

// VersionInfo contains build version information.
type VersionInfo struct {
	Version string `json:"version"`
	Date    string `json:"date"`
	Commit  string `json:"commit"`
}

// ServiceInfo contains information about a service.
type ServiceInfo struct {
	Status     string `json:"status"` // "running", "stopped", "unknown"
	Message    string `json:"message,omitempty"`
	ConfigHash string `json:"config_hash,omitempty"` // Hash of applied config (keen-pbr service only)
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

// CheckRequest contains the host (domain or IP) to check.
type CheckRequest struct {
	Host string `json:"host"`
}

// RoutingCheckResponse returns detailed routing information for a host.
type RoutingCheckResponse struct {
	Host              string              `json:"host"`
	ResolvedIPs       []string            `json:"resolved_ips,omitempty"`
	MatchedByHostname []HostnameRuleMatch `json:"matched_by_hostname,omitempty"`
	IPSetChecks       []IPSetCheckResult  `json:"ipset_checks"`
}

// HostnameRuleMatch represents a rule that matched by hostname pattern.
type HostnameRuleMatch struct {
	RuleName string `json:"rule_name"`
	Pattern  string `json:"pattern"`
}

// IPSetCheckResult contains check results for one IP address.
type IPSetCheckResult struct {
	IP          string            `json:"ip"`
	RuleResults []RuleCheckResult `json:"rule_results"`
}

// RuleCheckResult shows if an IP is present in a rule's IPSet.
type RuleCheckResult struct {
	RuleName        string `json:"rule_name"`
	PresentInIPSet  bool   `json:"present_in_ipset"`
	ShouldBePresent bool   `json:"should_be_present"`
	MatchReason     string `json:"match_reason,omitempty"` // e.g., "hostname acme.corp", "ipv4 1.2.3.0/24"
}

// PingCheckResponse returns ping results for a host.
type PingCheckResponse struct {
	Host        string  `json:"host"`
	ResolvedIP  string  `json:"resolved_ip,omitempty"`
	Success     bool    `json:"success"`
	PacketsSent int     `json:"packets_sent,omitempty"`
	PacketsRecv int     `json:"packets_received,omitempty"`
	PacketLoss  float64 `json:"packet_loss,omitempty"`
	MinRTT      float64 `json:"min_rtt,omitempty"` // milliseconds
	AvgRTT      float64 `json:"avg_rtt,omitempty"` // milliseconds
	MaxRTT      float64 `json:"max_rtt,omitempty"` // milliseconds
	Output      string  `json:"output,omitempty"`
	Error       string  `json:"error,omitempty"`
}

// TracerouteCheckResponse returns traceroute results for a host.
type TracerouteCheckResponse struct {
	Host       string          `json:"host"`
	ResolvedIP string          `json:"resolved_ip,omitempty"`
	Success    bool            `json:"success"`
	Hops       []TracerouteHop `json:"hops,omitempty"`
	Output     string          `json:"output,omitempty"`
	Error      string          `json:"error,omitempty"`
}

// TracerouteHop represents a single hop in a traceroute.
type TracerouteHop struct {
	Hop      int     `json:"hop"`
	IP       string  `json:"ip,omitempty"`
	Hostname string  `json:"hostname,omitempty"`
	RTT      float64 `json:"rtt,omitempty"` // milliseconds
}

// SelfCheckResponse returns self-check results as a table.
type SelfCheckResponse struct {
	Checks []SelfCheckRow `json:"checks"`
}

// SelfCheckRow represents a single row in the self-check table.
type SelfCheckRow struct {
	IPSet      string `json:"ipset"`      // IPSet name (empty for global checks)
	Validation string `json:"validation"` // Type of check (e.g., "ipset", "ip_rule", "iptables")
	Comment    string `json:"comment"`    // Explanation of what is being validated
	State      bool   `json:"state"`      // true = pass (✓), false = fail (✗)
	Message    string `json:"message"`    // Detailed message
}

// ValidationErrorDetail represents a single validation error.
type ValidationErrorDetail struct {
	Field   string `json:"field"`   // Field path (e.g., "routing.interfaces")
	Message string `json:"message"` // Error message
}
