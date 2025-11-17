package api

import (
	"fmt"
	"net/http"
	"os/exec"
	"strings"
)

// NetworkCheckResponse represents network check results
type NetworkCheckResponse struct {
	Checks        []Check  `json:"checks"`
	OverallStatus bool     `json:"overall_status"`
	FailedChecks  []string `json:"failed_checks"`
}

// Check represents a single check result
type Check struct {
	Name        string `json:"name"`
	Description string `json:"description"`
	Status      bool   `json:"status"`
	Message     string `json:"message"`
}

// IPSetCheckResponse represents ipset domain check results
type IPSetCheckResponse struct {
	Domain      string              `json:"domain"`
	IPSet       string              `json:"ipset"`
	Nameserver  string              `json:"nameserver"`
	Results     []IPSetCheckResult  `json:"results"`
	AllInSet    bool                `json:"all_in_set"`
}

// IPSetCheckResult represents a single IP check result
type IPSetCheckResult struct {
	IP    string `json:"ip"`
	InSet bool   `json:"in_set"`
}

// HandleCheckNetworking runs network configuration checks
func HandleCheckNetworking(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		checks := []Check{}
		failedChecks := []string{}

		// 1. Check config validity
		cfg, err := LoadConfig(configPath)
		if err != nil {
			checks = append(checks, Check{
				Name:        "config_valid",
				Description: "Configuration file is valid",
				Status:      false,
				Message:     fmt.Sprintf("Configuration validation failed: %v", err),
			})
			failedChecks = append(failedChecks, "config_valid")
		} else {
			checks = append(checks, Check{
				Name:        "config_valid",
				Description: "Configuration file is valid",
				Status:      true,
				Message:     "Configuration validated successfully",
			})

			// 2. Check ipsets exist
			allIPSetsExist := true
			var missingIPSets []string
			for _, ipset := range cfg.IPSets {
				out, err := exec.Command("ipset", "list", ipset.IPSetName).CombinedOutput()
				if err != nil || !strings.Contains(string(out), ipset.IPSetName) {
					allIPSetsExist = false
					missingIPSets = append(missingIPSets, ipset.IPSetName)
				}
			}

			if allIPSetsExist {
				checks = append(checks, Check{
					Name:        "ipsets_exist",
					Description: "All configured ipsets exist",
					Status:      true,
					Message:     "All ipsets found",
				})
			} else {
				checks = append(checks, Check{
					Name:        "ipsets_exist",
					Description: "All configured ipsets exist",
					Status:      false,
					Message:     fmt.Sprintf("Missing ipsets: %v", missingIPSets),
				})
				failedChecks = append(failedChecks, "ipsets_exist")
			}

			// 3. Check iptables rules
			out, err := exec.Command("iptables", "-t", "mangle", "-L", "-n").CombinedOutput()
			iptablesOk := err == nil && len(string(out)) > 0
			if iptablesOk {
				checks = append(checks, Check{
					Name:        "iptables_rules",
					Description: "IPTables rules are applied",
					Status:      true,
					Message:     "IPTables rules configured",
				})
			} else {
				checks = append(checks, Check{
					Name:        "iptables_rules",
					Description: "IPTables rules are applied",
					Status:      false,
					Message:     "IPTables rules not found or error checking",
				})
				failedChecks = append(failedChecks, "iptables_rules")
			}

			// 4. Check IP rules
			out, err = exec.Command("ip", "rule", "show").CombinedOutput()
			ipRulesOk := err == nil && len(string(out)) > 0
			if ipRulesOk {
				checks = append(checks, Check{
					Name:        "ip_rules",
					Description: "IP rules are configured",
					Status:      true,
					Message:     "All IP rules configured",
				})
			} else {
				checks = append(checks, Check{
					Name:        "ip_rules",
					Description: "IP rules are configured",
					Status:      false,
					Message:     "IP rules not found or error checking",
				})
				failedChecks = append(failedChecks, "ip_rules")
			}

			// 5. Check IP routes
			routesOk := true
			for _, ipset := range cfg.IPSets {
				if ipset.Routing != nil {
					out, err := exec.Command("ip", "route", "show", "table", fmt.Sprintf("%d", ipset.Routing.IpRouteTable)).CombinedOutput()
					if err != nil || len(string(out)) == 0 {
						routesOk = false
						break
					}
				}
			}

			if routesOk {
				checks = append(checks, Check{
					Name:        "ip_routes",
					Description: "IP routes are configured",
					Status:      true,
					Message:     "All routes configured",
				})
			} else {
				checks = append(checks, Check{
					Name:        "ip_routes",
					Description: "IP routes are configured",
					Status:      false,
					Message:     "Some routes missing",
				})
				failedChecks = append(failedChecks, "ip_routes")
			}

			// 6. Check interfaces available
			interfacesOk := true
			for _, ipset := range cfg.IPSets {
				if ipset.Routing != nil && len(ipset.Routing.Interfaces) > 0 {
					// Check if at least one interface is up
					anyUp := false
					for _, iface := range ipset.Routing.Interfaces {
						out, err := exec.Command("ip", "link", "show", iface).CombinedOutput()
						if err == nil && strings.Contains(string(out), "state UP") {
							anyUp = true
							break
						}
					}
					if !anyUp {
						interfacesOk = false
						break
					}
				}
			}

			if interfacesOk {
				checks = append(checks, Check{
					Name:        "interfaces_available",
					Description: "Routing interfaces are available",
					Status:      true,
					Message:     "Interfaces are available",
				})
			} else {
				checks = append(checks, Check{
					Name:        "interfaces_available",
					Description: "Routing interfaces are available",
					Status:      false,
					Message:     "Some interfaces are down",
				})
				failedChecks = append(failedChecks, "interfaces_available")
			}
		}

		resp := NetworkCheckResponse{
			Checks:        checks,
			OverallStatus: len(failedChecks) == 0,
			FailedChecks:  failedChecks,
		}

		RespondOK(w, resp)
	}
}

// HandleCheckIPSet checks if domain IPs are in ipset
func HandleCheckIPSet(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ipsetName := r.URL.Query().Get("ipset")
		domain := r.URL.Query().Get("domain")

		if ipsetName == "" {
			RespondValidationError(w, "Query parameter 'ipset' is required")
			return
		}

		if domain == "" {
			RespondValidationError(w, "Query parameter 'domain' is required")
			return
		}

		// Verify ipset exists
		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		ipsetExists := false
		for _, ipset := range cfg.IPSets {
			if ipset.IPSetName == ipsetName {
				ipsetExists = true
				break
			}
		}

		if !ipsetExists {
			RespondNotFound(w, fmt.Sprintf("IPSet '%s' not found", ipsetName))
			return
		}

		// Get nameserver (use fallback DNS or default)
		nameserver := "8.8.8.8"
		if cfg.General.FallbackDNS != "" {
			nameserver = cfg.General.FallbackDNS
		}

		// Perform nslookup
		out, err := exec.Command("nslookup", domain, nameserver).CombinedOutput()
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("DNS lookup failed: %v", err))
			return
		}

		// Parse IP addresses from nslookup output
		ips := []string{}
		lines := strings.Split(string(out), "\n")
		for _, line := range lines {
			line = strings.TrimSpace(line)
			if strings.HasPrefix(line, "Address:") {
				parts := strings.Fields(line)
				if len(parts) >= 2 && parts[1] != nameserver {
					ips = append(ips, parts[1])
				}
			}
		}

		if len(ips) == 0 {
			RespondInternalError(w, "No IP addresses resolved for domain")
			return
		}

		// Check each IP in ipset
		results := []IPSetCheckResult{}
		allInSet := true
		for _, ip := range ips {
			err := exec.Command("ipset", "test", ipsetName, ip).Run()
			inSet := err == nil

			results = append(results, IPSetCheckResult{
				IP:    ip,
				InSet: inSet,
			})

			if !inSet {
				allInSet = false
			}
		}

		resp := IPSetCheckResponse{
			Domain:     domain,
			IPSet:      ipsetName,
			Nameserver: nameserver,
			Results:    results,
			AllInSet:   allInSet,
		}

		RespondOK(w, resp)
	}
}
