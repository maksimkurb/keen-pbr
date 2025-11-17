package api

import (
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"strings"
)

// StatusResponse represents system status
type StatusResponse struct {
	KeenPBRVersion        string                  `json:"keen_pbr_version"`
	KeeneticOSVersion     string                  `json:"keenetic_os_version"`
	DnsmasqStatus         string                  `json:"dnsmasq_status"`
	KeenPBRServiceStatus  string                  `json:"keen_pbr_service_status"`
	Services              map[string]ServiceStatus `json:"services"`
}

// ServiceStatus represents status of a service
type ServiceStatus struct {
	Status  string `json:"status"`
	Message string `json:"message,omitempty"`
	PID     int    `json:"pid,omitempty"`
}

// HandleStatus returns system status
func HandleStatus(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Get keen-pbr version
		versionBytes, err := os.ReadFile("/home/user/keen-pbr/VERSION")
		version := "unknown"
		if err == nil {
			version = strings.TrimSpace(string(versionBytes))
		}

		// Get dnsmasq status
		dnsmasqStatus := "unknown"
		dnsmasqMessage := ""
		out, err := exec.Command("/opt/etc/init.d/S56dnsmasq", "check").CombinedOutput()
		if err == nil {
			output := string(out)
			if strings.Contains(output, "alive") {
				dnsmasqStatus = "alive"
				dnsmasqMessage = "alive"
			} else {
				dnsmasqStatus = "dead"
			}
		}

		// Get keen-pbr service status
		keenPBRStatus := "unknown"
		keenPBRMessage := ""
		out, err = exec.Command("/opt/etc/init.d/S80keen-pbr", "check").CombinedOutput()
		if err == nil {
			output := string(out)
			if strings.Contains(output, "alive") || strings.Contains(output, "running") {
				keenPBRStatus = "running"
				keenPBRMessage = "running"
			} else {
				keenPBRStatus = "stopped"
			}
		}

		resp := StatusResponse{
			KeenPBRVersion:       version,
			KeeneticOSVersion:    "unknown", // TODO: Query Keenetic API
			DnsmasqStatus:        dnsmasqStatus,
			KeenPBRServiceStatus: keenPBRStatus,
			Services: map[string]ServiceStatus{
				"dnsmasq": {
					Status:  dnsmasqStatus,
					Message: dnsmasqMessage,
				},
				"keen_pbr": {
					Status:  keenPBRStatus,
					Message: keenPBRMessage,
				},
			},
		}

		RespondOK(w, resp)
	}
}
