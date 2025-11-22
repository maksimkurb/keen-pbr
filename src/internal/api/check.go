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
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
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
	defer func() {
		if r := recover(); r != nil {
			log.Errorf("Panic in streamOutput: %v", r)
		}
	}()

	if reader == nil || w == nil || flusher == nil {
		log.Errorf("streamOutput called with nil parameters")
		return
	}

	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		line := scanner.Text()
		// Escape any special characters for SSE
		line = strings.ReplaceAll(line, "\n", "")

		// Safely write and flush
		if _, err := fmt.Fprintf(w, "data: %s\n\n", line); err != nil {
			log.Debugf("Failed to write to response: %v", err)
			return
		}

		// Recover from flush panics
		func() {
			defer func() {
				if r := recover(); r != nil {
					log.Debugf("Panic during flush (client likely disconnected): %v", r)
				}
			}()
			flusher.Flush()
		}()
	}
}

// CheckSelf performs a self-check similar to the self-check command.
// GET /api/v1/check/self?sse=true (SSE streaming) or ?sse=false (JSON table, default)
func (h *Handler) CheckSelf(w http.ResponseWriter, r *http.Request) {
	// Check if SSE mode is requested
	sseMode := r.URL.Query().Get("sse") == "true"

	if sseMode {
		h.checkSelfSSE(w, r)
	} else {
		h.checkSelfJSON(w, r)
	}
}

// checkSelfJSON performs self-check and returns results as a JSON table
func (h *Handler) checkSelfJSON(w http.ResponseWriter, _ *http.Request) {
	checks := []SelfCheckRow{}

	// Load configuration
	cfg, err := h.loadConfig()
	if err != nil {
		checks = append(checks, SelfCheckRow{
			IPSet:      "",
			Validation: "config",
			Comment:    "Global configuration check",
			State:      false,
			Message:    fmt.Sprintf("Failed to load configuration: %v", err),
		})
		writeJSONData(w, SelfCheckResponse{Checks: checks})
		return
	}

	// Validate configuration
	if err := cfg.ValidateConfig(); err != nil {
		checks = append(checks, SelfCheckRow{
			IPSet:      "",
			Validation: "config_validation",
			Comment:    "Configuration validation ensures all required fields are present and valid",
			State:      false,
			Message:    fmt.Sprintf("Configuration validation failed: %v", err),
		})
	} else {
		checks = append(checks, SelfCheckRow{
			IPSet:      "",
			Validation: "config_validation",
			Comment:    "Configuration validation ensures all required fields are present and valid",
			State:      true,
			Message:    "Configuration is valid",
		})
	}

	// Check each IPSet
	for _, ipsetCfg := range cfg.IPSets {
		ipsetChecks := h.checkIPSetSelfJSON(ipsetCfg)
		checks = append(checks, ipsetChecks...)
	}

	writeJSONData(w, SelfCheckResponse{Checks: checks})
}

// checkSelfSSE performs self-check and streams results via Server-Sent Events
func (h *Handler) checkSelfSSE(w http.ResponseWriter, _ *http.Request) {
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
		h.sendCheckEventWithContext(w, flusher, "config", "", false, "Global configuration check", fmt.Sprintf("Failed to load configuration: %v", err))
		return
	}

	// Validate configuration
	if err := cfg.ValidateConfig(); err != nil {
		h.sendCheckEventWithContext(w, flusher, "config_validation", "", false, "Configuration validation ensures all required fields are present and valid", fmt.Sprintf("Configuration validation failed: %v", err))
		hasFailures = true
	} else {
		h.sendCheckEventWithContext(w, flusher, "config_validation", "", true, "Configuration validation ensures all required fields are present and valid", "Configuration is valid")
	}

	// Check each IPSet
	for _, ipsetCfg := range cfg.IPSets {
		if !h.checkIPSetSelfSSE(w, flusher, ipsetCfg) {
			hasFailures = true
		}
	}

	// Send completion message
	if hasFailures {
		h.sendCheckEventWithContext(w, flusher, "complete", "", false, "", "Self-check completed with failures")
	} else {
		h.sendCheckEventWithContext(w, flusher, "complete", "", true, "", "Self-check completed successfully")
	}
}

// checkIPSetSelfSSE checks a single IPSet configuration and streams results via SSE
// This implementation uses the NetworkingComponent abstraction (identical logic to JSON mode)
// Returns true if all checks passed, false if any failed
func (h *Handler) checkIPSetSelfSSE(w http.ResponseWriter, flusher http.Flusher, ipsetCfg *config.IPSetConfig) bool {
	hasFailures := false

	// Build components for this IPSet using the networking component abstraction
	// Convert domain.KeeneticClient to concrete type for component builder
	var keeneticClient *keenetic.Client
	if h.deps != nil {
		if concreteClient, ok := h.deps.KeeneticClient().(*keenetic.Client); ok {
			keeneticClient = concreteClient
		}
	}

	builder := networking.NewComponentBuilder(keeneticClient)
	components, err := builder.BuildComponents(ipsetCfg)
	if err != nil {
		h.sendCheckEventWithContext(w, flusher, "component_build", ipsetCfg.IPSetName, false,
			"Failed to build networking components", fmt.Sprintf("Error: %v", err))
		return false
	}

	// Check each component using the unified abstraction
	for _, component := range components {
		exists, err := component.IsExists()
		shouldExist := component.ShouldExist()

		// State is OK if actual existence matches expected existence and no error occurred
		state := (exists == shouldExist) && err == nil

		var message string
		if err != nil {
			message = fmt.Sprintf("Error checking: %v", err)
			state = false
		} else {
			message = h.getComponentMessage(component, exists, shouldExist, ipsetCfg)
		}

		// Send SSE event
		h.sendCheckEventWithContext(w, flusher,
			string(component.GetType()),
			component.GetIPSetName(),
			state,
			component.GetDescription(),
			message,
		)

		if !state {
			hasFailures = true
		}
	}

	return !hasFailures
}

// checkIPSetSelfJSON checks a single IPSet configuration and returns results as table rows
// This implementation uses the NetworkingComponent abstraction instead of direct command execution
func (h *Handler) checkIPSetSelfJSON(ipsetCfg *config.IPSetConfig) []SelfCheckRow {
	checks := []SelfCheckRow{}

	// Build components for this IPSet using the networking component abstraction
	// Convert domain.KeeneticClient to concrete type for component builder
	var keeneticClient *keenetic.Client
	if h.deps != nil {
		if concreteClient, ok := h.deps.KeeneticClient().(*keenetic.Client); ok {
			keeneticClient = concreteClient
		}
	}

	builder := networking.NewComponentBuilder(keeneticClient)
	components, err := builder.BuildComponents(ipsetCfg)
	if err != nil {
		checks = append(checks, SelfCheckRow{
			IPSet:      ipsetCfg.IPSetName,
			Validation: "component_build",
			Comment:    "Failed to build networking components",
			State:      false,
			Message:    fmt.Sprintf("Error: %v", err),
		})
		return checks
	}

	// Check each component using the unified abstraction
	for _, component := range components {
		exists, err := component.IsExists()
		shouldExist := component.ShouldExist()

		// State is OK if actual existence matches expected existence and no error occurred
		state := (exists == shouldExist) && err == nil

		var message string
		if err != nil {
			message = fmt.Sprintf("Error checking: %v", err)
			state = false
		} else {
			message = h.getComponentMessage(component, exists, shouldExist, ipsetCfg)
		}

		checks = append(checks, SelfCheckRow{
			IPSet:      component.GetIPSetName(),
			Validation: string(component.GetType()),
			Comment:    component.GetDescription(),
			State:      state,
			Message:    message,
		})
	}

	return checks
}

// getComponentMessage generates an appropriate message based on component type and state
func (h *Handler) getComponentMessage(component networking.NetworkingComponent, exists bool, shouldExist bool, ipsetCfg *config.IPSetConfig) string {
	compType := component.GetType()

	switch compType {
	case networking.ComponentTypeIPSet:
		if exists && shouldExist {
			return fmt.Sprintf("IPSet [%s] exists", component.GetIPSetName())
		} else if !exists && shouldExist {
			return fmt.Sprintf("IPSet [%s] does NOT exist (missing)", component.GetIPSetName())
		} else if exists && !shouldExist {
			return fmt.Sprintf("IPSet [%s] exists but should NOT (unexpected)", component.GetIPSetName())
		}
		return fmt.Sprintf("IPSet [%s] not present", component.GetIPSetName())

	case networking.ComponentTypeIPRule:
		if exists && shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x lookup %d exists",
				ipsetCfg.Routing.FwMark, ipsetCfg.Routing.IPRouteTable)
		} else if !exists && shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x does NOT exist (missing)",
				ipsetCfg.Routing.FwMark)
		} else if exists && !shouldExist {
			return fmt.Sprintf("IP rule with fwmark 0x%x exists but should NOT (unexpected)",
				ipsetCfg.Routing.FwMark)
		}
		return fmt.Sprintf("IP rule with fwmark 0x%x not present", ipsetCfg.Routing.FwMark)

	case networking.ComponentTypeIPRoute:
		if routeComp, ok := component.(*networking.IPRouteComponent); ok {
			if routeComp.GetRouteType() == networking.RouteTypeBlackhole {
				if exists && shouldExist {
					return fmt.Sprintf("Blackhole route in table %d exists (kill-switch enabled)",
						ipsetCfg.Routing.IPRouteTable)
				} else if !exists && !shouldExist {
					return fmt.Sprintf("Blackhole route in table %d not present (kill-switch disabled)",
						ipsetCfg.Routing.IPRouteTable)
				} else if exists && !shouldExist {
					return fmt.Sprintf("Blackhole route in table %d exists but kill-switch is DISABLED (stale)",
						ipsetCfg.Routing.IPRouteTable)
				}
				return fmt.Sprintf("Blackhole route in table %d missing but kill-switch is ENABLED (missing)",
					ipsetCfg.Routing.IPRouteTable)
			} else {
				ifaceName := routeComp.GetInterfaceName()
				if exists && shouldExist {
					return fmt.Sprintf("Route in table %d via %s exists (active)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				} else if !exists && !shouldExist {
					return fmt.Sprintf("Route in table %d via %s not present (interface not best)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				} else if exists && !shouldExist {
					return fmt.Sprintf("Route in table %d via %s exists but is not best interface (stale)",
						ipsetCfg.Routing.IPRouteTable, ifaceName)
				}
				return fmt.Sprintf("Route in table %d via %s missing but is best interface (missing)",
					ipsetCfg.Routing.IPRouteTable, ifaceName)
			}
		}

	case networking.ComponentTypeIPTables:
		if iptComp, ok := component.(*networking.IPTablesRuleComponent); ok {
			ruleDesc := iptComp.GetRuleDescription()
			if exists && shouldExist {
				return fmt.Sprintf("IPTables %s exists", ruleDesc)
			} else if !exists && shouldExist {
				return fmt.Sprintf("IPTables %s does NOT exist (missing)", ruleDesc)
			} else if exists && !shouldExist {
				return fmt.Sprintf("IPTables %s exists but should NOT (unexpected)", ruleDesc)
			}
			return fmt.Sprintf("IPTables %s not present", ruleDesc)
		}
	}

	// Fallback generic message
	if exists && shouldExist {
		return "Component exists as expected"
	} else if !exists && !shouldExist {
		return "Component absent as expected"
	} else if exists && !shouldExist {
		return "Component exists but should NOT (unexpected)"
	}
	return "Component missing but should exist"
}

// sendCheckEventWithContext sends a JSON check event via SSE with additional context
func (h *Handler) sendCheckEventWithContext(w http.ResponseWriter, flusher http.Flusher, check string, ipsetName string, ok bool, reason string, logMsg string) {
	event := map[string]interface{}{
		"check":  check,
		"ok":     ok,
		"log":    logMsg,
		"reason": reason,
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

// CheckSplitDNS streams DNS queries received on the DNS check port via SSE.
// GET /api/v1/check/split-dns
func (h *Handler) CheckSplitDNS(w http.ResponseWriter, r *http.Request) {
	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")

	flusher, ok := w.(http.Flusher)
	if !ok {
		WriteInternalError(w, "Streaming not supported")
		return
	}

	// Get DNS check subscriber from handler dependencies
	if h.dnsCheckSubscriber == nil {
		WriteInternalError(w, "DNS check not available")
		return
	}

	// Subscribe to DNS check events
	eventCh := h.dnsCheckSubscriber.Subscribe()
	defer h.dnsCheckSubscriber.Unsubscribe(eventCh)

	log.Debugf("Client connected to split-DNS check SSE stream")

	// Send "connected" message to confirm connection is established
	if _, err := fmt.Fprintf(w, "data: connected\n\n"); err != nil {
		log.Debugf("Failed to send connected message: %v", err)
		return
	}
	flusher.Flush()

	// Stream events to client
	for {
		select {
		case <-r.Context().Done():
			// Client disconnected
			log.Debugf("Client disconnected from split-DNS check SSE stream")
			return
		case domain := <-eventCh:
			// Send domain as SSE event
			if _, err := fmt.Fprintf(w, "data: %s\n\n", domain); err != nil {
				log.Debugf("Failed to write to response: %v", err)
				return
			}
			flusher.Flush()
			log.Debugf("Sent DNS check domain to SSE client: %s", domain)
		}
	}
}
