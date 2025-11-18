package api

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"net/http"
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

			// Check if this is a domain match (only for inline hosts)
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
		}
	nextIPSet:
	}

	return matches
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
