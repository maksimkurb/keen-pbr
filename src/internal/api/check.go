package api

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strings"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// CheckRouting checks routing information for a given host.
// POST /api/v1/check/routing
func (h *Handler) CheckRouting(w http.ResponseWriter, r *http.Request) {
	var req CheckRequest
	if err := decodeJSON(r, &req); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	if req.Host == "" {
		WriteInvalidRequest(w, "Host is required")
		return
	}

	// Load configuration
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	response := RoutingCheckResponse{
		Host:              req.Host,
		MatchedByHostname: []HostnameRuleMatch{},
		IPSetChecks:       []IPSetCheckResult{},
	}

	// Try to resolve the host
	ips, err := net.LookupHost(req.Host)
	if err != nil {
		// It might be an IP address already
		if ip := net.ParseIP(req.Host); ip != nil {
			response.ResolvedIPs = []string{req.Host}
			ips = []string{req.Host}
		} else {
			log.Debugf("Failed to resolve host %s: %v", req.Host, err)
		}
	} else {
		response.ResolvedIPs = ips
	}

	// Check which rules match by hostname pattern
	hostnameMatches := h.findHostnameMatches(cfg, req.Host)
	response.MatchedByHostname = hostnameMatches

	// Build map of rules that should contain IPs (matched by hostname)
	shouldContainByHostname := make(map[string]bool)
	for _, match := range hostnameMatches {
		shouldContainByHostname[match.RuleName] = true
	}

	// For each resolved IP, check against all rules
	for _, ip := range ips {
		ipCheck := IPSetCheckResult{
			IP:          ip,
			RuleResults: []RuleCheckResult{},
		}

		for _, ipsetConfig := range cfg.IPSets {
			ruleName := ipsetConfig.IPSetName

			// Test if IP is actually in the IPSet
			presentInIPSet := h.testIPInIPSet(ruleName, ip)

			// Determine if it should be present
			shouldBePresent := false
			matchReason := ""

			// Should be present if hostname matched
			if shouldContainByHostname[ruleName] {
				shouldBePresent = true
				matchReason = "hostname match"
			}

			// Should be present if IP matches a list in this rule
			if !shouldBePresent {
				if reason := h.checkIPMatchesLists(cfg, ipsetConfig, ip); reason != "" {
					shouldBePresent = true
					matchReason = reason
				}
			}

			ipCheck.RuleResults = append(ipCheck.RuleResults, RuleCheckResult{
				RuleName:        ruleName,
				PresentInIPSet:  presentInIPSet,
				ShouldBePresent: shouldBePresent,
				MatchReason:     matchReason,
			})
		}

		response.IPSetChecks = append(response.IPSetChecks, ipCheck)
	}

	writeJSONData(w, response)
}

// findHostnameMatches finds which rules match the hostname by domain pattern
func (h *Handler) findHostnameMatches(cfg *config.Config, host string) []HostnameRuleMatch {
	matches := []HostnameRuleMatch{}

	for _, ipsetConfig := range cfg.IPSets {
		// Check each list in this IPSet
		for _, listName := range ipsetConfig.Lists {
			// Find the list config
			var listSource *config.ListSource
			for _, ls := range cfg.Lists {
				if ls.Name() == listName {
					listSource = ls
					break
				}
			}

			if listSource == nil {
				continue
			}

			// Check inline hosts
			if listSource.Hosts != nil {
				for _, domainPattern := range listSource.Hosts {
					if h.matchesDomain(host, domainPattern) {
						matches = append(matches, HostnameRuleMatch{
							RuleName: ipsetConfig.IPSetName,
							Pattern:  domainPattern,
						})
						goto nextIPSet
					}
				}
			}

			// Check file-based or URL-based lists
			if listSource.URL != "" || listSource.File != "" {
				if pattern := h.checkHostInListFile(cfg, listSource, host); pattern != "" {
					matches = append(matches, HostnameRuleMatch{
						RuleName: ipsetConfig.IPSetName,
						Pattern:  pattern,
					})
					goto nextIPSet
				}
			}
		}
	nextIPSet:
	}

	return matches
}

// checkHostInListFile checks if a host matches any domain pattern in a list file
func (h *Handler) checkHostInListFile(cfg *config.Config, listSource *config.ListSource, host string) string {
	// Get the file path
	filePath, err := listSource.GetAbsolutePathAndCheckExists(cfg)
	if err != nil {
		log.Debugf("Failed to get list file path: %v", err)
		return ""
	}

	// Open and read the file
	file, err := os.Open(filePath)
	if err != nil {
		log.Debugf("Failed to open list file %s: %v", filePath, err)
		return ""
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Check if this line is a domain pattern that matches
		if h.matchesDomain(host, line) {
			return line
		}
	}

	return ""
}

// checkIPMatchesLists checks if an IP matches any list patterns in an IPSet
// Returns the match reason if found, empty string otherwise
func (h *Handler) checkIPMatchesLists(cfg *config.Config, ipsetConfig *config.IPSetConfig, ip string) string {
	// For now, we can't easily check IP/CIDR matches without parsing all list files
	// We rely on the ipset test command to tell us if it's present
	// This function could be enhanced to check inline IP lists or CIDR ranges
	return ""
}

// testIPInIPSet tests if an IP is in an IPSet using the ipset test command
func (h *Handler) testIPInIPSet(ipsetName, ip string) bool {
	// Run: ipset test <ipset_name> <ip>
	cmd := exec.Command("ipset", "test", ipsetName, ip)
	err := cmd.Run()

	// ipset test returns exit code 0 if IP is in the set, 1 if not
	return err == nil
}

// matchesDomain checks if a host matches a domain pattern (supports wildcards)
func (h *Handler) matchesDomain(host, pattern string) bool {
	// Exact match
	if host == pattern {
		return true
	}

	// Wildcard subdomain match (e.g., *.example.com matches sub.example.com)
	if strings.HasPrefix(pattern, "*.") {
		baseDomain := pattern[2:]
		if host == baseDomain || strings.HasSuffix(host, "."+baseDomain) {
			return true
		}
	}

	return false
}

// CheckPing performs a ping to the given host and streams output via SSE.
// GET /api/v1/check/ping?host=example.com
func (h *Handler) CheckPing(w http.ResponseWriter, r *http.Request) {
	host := r.URL.Query().Get("host")
	if host == "" {
		WriteInvalidRequest(w, "Host parameter is required")
		return
	}

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no") // Disable nginx buffering

	flusher, ok := w.(http.Flusher)
	if !ok {
		WriteInternalError(w, "Streaming not supported")
		return
	}

	// Run ping command (4 packets, 2 second timeout)
	cmd := exec.Command("ping", "-c", "4", "-W", "2", host)

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		fmt.Fprintf(w, "data: Error: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	stderr, err := cmd.StderrPipe()
	if err != nil {
		fmt.Fprintf(w, "data: Error: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	if err := cmd.Start(); err != nil {
		fmt.Fprintf(w, "data: Error starting ping: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	// Stream stdout
	go h.streamOutput(stdout, w, flusher)

	// Stream stderr
	go h.streamOutput(stderr, w, flusher)

	// Wait for command to finish
	if err := cmd.Wait(); err != nil {
		fmt.Fprintf(w, "data: [Process exited with error: %s]\n\n", err.Error())
	} else {
		fmt.Fprintf(w, "data: [Process completed successfully]\n\n")
	}
	flusher.Flush()
}

// CheckTraceroute performs a traceroute to the given host and streams output via SSE.
// GET /api/v1/check/traceroute?host=example.com
func (h *Handler) CheckTraceroute(w http.ResponseWriter, r *http.Request) {
	host := r.URL.Query().Get("host")
	if host == "" {
		WriteInvalidRequest(w, "Host parameter is required")
		return
	}

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no") // Disable nginx buffering

	flusher, ok := w.(http.Flusher)
	if !ok {
		WriteInternalError(w, "Streaming not supported")
		return
	}

	// Run traceroute command (max 30 hops, 2 second timeout per hop)
	cmd := exec.Command("traceroute", "-m", "30", "-w", "2", host)

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		fmt.Fprintf(w, "data: Error: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	stderr, err := cmd.StderrPipe()
	if err != nil {
		fmt.Fprintf(w, "data: Error: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	if err := cmd.Start(); err != nil {
		fmt.Fprintf(w, "data: Error starting traceroute: %s\n\n", err.Error())
		flusher.Flush()
		return
	}

	// Stream stdout
	go h.streamOutput(stdout, w, flusher)

	// Stream stderr
	go h.streamOutput(stderr, w, flusher)

	// Wait for command to finish
	if err := cmd.Wait(); err != nil {
		fmt.Fprintf(w, "data: [Process exited with error: %s]\n\n", err.Error())
	} else {
		fmt.Fprintf(w, "data: [Process completed successfully]\n\n")
	}
	flusher.Flush()
}

// streamOutput reads from a reader and streams each line as an SSE event
func (h *Handler) streamOutput(reader io.Reader, w http.ResponseWriter, flusher http.Flusher) {
	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		line := scanner.Text()
		// Escape any special characters for SSE
		line = strings.ReplaceAll(line, "\n", "")
		fmt.Fprintf(w, "data: %s\n\n", line)
		flusher.Flush()
	}
}

// CheckSelf performs a self-check similar to the self-check command and streams results via SSE.
// GET /api/v1/check/self
func (h *Handler) CheckSelf(w http.ResponseWriter, r *http.Request) {
	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no") // Disable nginx buffering

	flusher, ok := w.(http.Flusher)
	if !ok {
		WriteInternalError(w, "Streaming not supported")
		return
	}

	// Track if any check failed
	hasFailures := false

	// Load configuration
	cfg, err := h.loadConfig()
	if err != nil {
		h.sendCheckEventWithContext(w, flusher, "config", "", false, "Global configuration check", fmt.Sprintf("Failed to load configuration: %v", err), "")
		return
	}

	// Validate configuration
	if err := cfg.ValidateConfig(); err != nil {
		h.sendCheckEventWithContext(w, flusher, "config_validation", "", false, "Configuration validation ensures all required fields are present and valid", fmt.Sprintf("Configuration validation failed: %v", err), "")
		hasFailures = true
	} else {
		h.sendCheckEventWithContext(w, flusher, "config_validation", "", true, "Configuration validation ensures all required fields are present and valid", "Configuration is valid", "")
	}

	// Check each IPSet
	for _, ipsetCfg := range cfg.IPSets {
		if !h.checkIPSetSelf(w, flusher, cfg, ipsetCfg) {
			hasFailures = true
		}
	}

	// Send completion message
	if hasFailures {
		h.sendCheckEventWithContext(w, flusher, "complete", "", false, "", "Self-check completed with failures", "")
	} else {
		h.sendCheckEventWithContext(w, flusher, "complete", "", true, "", "Self-check completed successfully", "")
	}
}

// checkIPSetSelf checks a single IPSet configuration and streams results
// Returns true if all checks passed, false if any failed
func (h *Handler) checkIPSetSelf(w http.ResponseWriter, flusher http.Flusher, cfg *config.Config, ipsetCfg *config.IPSetConfig) bool {
	hasFailures := false
	ipsetName := ipsetCfg.IPSetName

	h.sendCheckEventWithContext(w, flusher, "ipset_start", ipsetName, true, "", fmt.Sprintf("Checking IPSet: %s", ipsetName), "")

	// Check if ipset exists
	cmd := exec.Command("ipset", "list", ipsetName)
	err := cmd.Run()
	ipsetCommand := fmt.Sprintf("ipset list %s", ipsetName)
	if err != nil {
		h.sendCheckEventWithContext(w, flusher, "ipset", ipsetName, false, "IPSet must exist to store IP addresses/subnets for policy routing", fmt.Sprintf("IPSet [%s] does NOT exist", ipsetName), ipsetCommand)
		hasFailures = true
	} else {
		h.sendCheckEventWithContext(w, flusher, "ipset", ipsetName, true, "IPSet must exist to store IP addresses/subnets for policy routing", fmt.Sprintf("IPSet [%s] exists", ipsetName), ipsetCommand)
	}

	// Check ip rule
	ipRuleCommand := fmt.Sprintf("ip rule add from all fwmark 0x%x lookup %d prio %d",
		ipsetCfg.Routing.FwMark, ipsetCfg.Routing.IpRouteTable, ipsetCfg.Routing.IpRulePriority)
	cmd = exec.Command("ip", "rule", "show")
	output, err := cmd.Output()
	if err != nil {
		h.sendCheckEventWithContext(w, flusher, "ip_rule", ipsetName, false, "IP rule routes packets marked by iptables to the custom routing table", fmt.Sprintf("Failed to check IP rule: %v", err), ipRuleCommand)
		hasFailures = true
	} else {
		if strings.Contains(string(output), fmt.Sprintf("fwmark 0x%x", ipsetCfg.Routing.FwMark)) {
			h.sendCheckEventWithContext(w, flusher, "ip_rule", ipsetName, true, "IP rule routes packets marked by iptables to the custom routing table", fmt.Sprintf("IP rule with fwmark 0x%x lookup %d exists", ipsetCfg.Routing.FwMark, ipsetCfg.Routing.IpRouteTable), ipRuleCommand)
		} else {
			h.sendCheckEventWithContext(w, flusher, "ip_rule", ipsetName, false, "IP rule routes packets marked by iptables to the custom routing table", fmt.Sprintf("IP rule with fwmark 0x%x does NOT exist", ipsetCfg.Routing.FwMark), ipRuleCommand)
			hasFailures = true
		}
	}

	// Check ip routes - list all routes in the table
	cmd = exec.Command("ip", "route", "show", "table", fmt.Sprintf("%d", ipsetCfg.Routing.IpRouteTable))
	output, err = cmd.Output()
	if err != nil {
		ipRouteCommand := fmt.Sprintf("ip route show table %d", ipsetCfg.Routing.IpRouteTable)
		h.sendCheckEventWithContext(w, flusher, "ip_route", ipsetName, false, "IP routes define the gateway/interface for packets in the custom routing table", fmt.Sprintf("Failed to check IP routes in table %d: %v", ipsetCfg.Routing.IpRouteTable, err), ipRouteCommand)
		hasFailures = true
	} else {
		// Parse each route and send individual events
		routes := strings.Split(strings.TrimSpace(string(output)), "\n")
		if len(routes) == 0 || (len(routes) == 1 && routes[0] == "") {
			ipRouteCommand := fmt.Sprintf("ip route add default via <gateway> dev <interface> table %d", ipsetCfg.Routing.IpRouteTable)
			h.sendCheckEventWithContext(w, flusher, "ip_route", ipsetName, false, "IP routes define the gateway/interface for packets in the custom routing table", fmt.Sprintf("No routes found in table %d", ipsetCfg.Routing.IpRouteTable), ipRouteCommand)
			hasFailures = true
		} else {
			for _, route := range routes {
				if route == "" {
					continue
				}
				ipRouteCommand := fmt.Sprintf("ip route show table %d", ipsetCfg.Routing.IpRouteTable)
				h.sendCheckEventWithContext(w, flusher, "ip_route", ipsetName, true, "IP routes define the gateway/interface for packets in the custom routing table", fmt.Sprintf("Route in table %d: %s", ipsetCfg.Routing.IpRouteTable, route), ipRouteCommand)
			}
		}
	}

	// Check iptables rules
	if ipsetCfg.IPTablesRules != nil && len(ipsetCfg.IPTablesRules) > 0 {
		for idx, rule := range ipsetCfg.IPTablesRules {
			if !h.checkIPTablesRule(w, flusher, ipsetCfg, rule, idx) {
				hasFailures = true
			}
		}
	}

	h.sendCheckEventWithContext(w, flusher, "ipset_end", ipsetName, true, "", fmt.Sprintf("Completed checking IPSet: %s", ipsetName), "")
	return !hasFailures
}

// checkIPTablesRule checks if an iptables rule exists
// Returns true if rule exists, false otherwise
func (h *Handler) checkIPTablesRule(w http.ResponseWriter, flusher http.Flusher, ipsetCfg *config.IPSetConfig, rule *config.IPTablesRule, idx int) bool {
	// Build the iptables check command
	args := []string{"-t", rule.Table, "-C", rule.Chain}

	// Expand template variables in rule
	expandedRule := make([]string, len(rule.Rule))
	for i, arg := range rule.Rule {
		expanded := arg
		expanded = strings.ReplaceAll(expanded, "{{ipset_name}}", ipsetCfg.IPSetName)
		expanded = strings.ReplaceAll(expanded, "{{fwmark}}", fmt.Sprintf("0x%x", ipsetCfg.Routing.FwMark))
		expanded = strings.ReplaceAll(expanded, "{{table}}", fmt.Sprintf("%d", ipsetCfg.Routing.IpRouteTable))
		expanded = strings.ReplaceAll(expanded, "{{priority}}", fmt.Sprintf("%d", ipsetCfg.Routing.IpRulePriority))
		expandedRule[i] = expanded
	}

	args = append(args, expandedRule...)

	var cmd *exec.Cmd
	var iptablesCmd string
	if ipsetCfg.IPVersion == 4 {
		cmd = exec.Command("iptables", args...)
		// Build the command for manual execution (using -A for add instead of -C for check)
		addArgs := append([]string{"-t", rule.Table, "-A", rule.Chain}, expandedRule...)
		iptablesCmd = "iptables " + strings.Join(addArgs, " ")
	} else {
		cmd = exec.Command("ip6tables", args...)
		addArgs := append([]string{"-t", rule.Table, "-A", rule.Chain}, expandedRule...)
		iptablesCmd = "ip6tables " + strings.Join(addArgs, " ")
	}

	err := cmd.Run()
	ruleDesc := fmt.Sprintf("%s/%s rule #%d", rule.Table, rule.Chain, idx+1)

	if err != nil {
		h.sendCheckEventWithContext(w, flusher, "iptables", ipsetCfg.IPSetName, false, "IPTables rule marks packets matching the ipset with fwmark for policy routing", fmt.Sprintf("IPTables %s does NOT exist", ruleDesc), iptablesCmd)
		return false
	} else {
		h.sendCheckEventWithContext(w, flusher, "iptables", ipsetCfg.IPSetName, true, "IPTables rule marks packets matching the ipset with fwmark for policy routing", fmt.Sprintf("IPTables %s exists", ruleDesc), iptablesCmd)
		return true
	}
}

// sendCheckEventWithContext sends a JSON check event via SSE with additional context
func (h *Handler) sendCheckEventWithContext(w http.ResponseWriter, flusher http.Flusher, check string, ipsetName string, ok bool, reason string, logMsg string, command string) {
	event := map[string]interface{}{
		"check":   check,
		"ok":      ok,
		"log":     logMsg,
		"reason":  reason,
		"command": command,
	}

	// Add ipset_name only if it's not empty
	if ipsetName != "" {
		event["ipset_name"] = ipsetName
	}

	jsonData, err := json.Marshal(event)
	if err != nil {
		log.Errorf("Failed to marshal check event: %v", err)
		return
	}

	fmt.Fprintf(w, "data: %s\n\n", string(jsonData))
	flusher.Flush()
}
