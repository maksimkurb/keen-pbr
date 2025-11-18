package api

import (
	"fmt"
	"net"
	"net/http"
	"os/exec"
	"regexp"
	"strconv"
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
		Host:          req.Host,
		MatchedIPSets: []IPSetMatch{},
	}

	// Try to resolve the host
	ips, err := net.LookupHost(req.Host)
	if err != nil {
		// It might be an IP address already
		if ip := net.ParseIP(req.Host); ip != nil {
			response.ResolvedIPs = []string{req.Host}
		} else {
			log.Debugf("Failed to resolve host %s: %v", req.Host, err)
		}
	} else {
		response.ResolvedIPs = ips
	}

	// Check which IPSets contain this host
	matchedIPSets := h.findMatchingIPSets(cfg, req.Host, response.ResolvedIPs)
	response.MatchedIPSets = matchedIPSets

	// Get routing information from the first matched IPSet
	if len(matchedIPSets) > 0 {
		// Find the IPSet config for the first match
		for _, ipsetConfig := range cfg.IPSets {
			if ipsetConfig.IPSetName == matchedIPSets[0].IPSetName {
				if ipsetConfig.Routing != nil {
					response.Routing = &RoutingInfo{
						Table:    fmt.Sprintf("%d", ipsetConfig.Routing.IpRouteTable),
						Priority: ipsetConfig.Routing.IpRulePriority,
					}
					if ipsetConfig.Routing.FwMark != 0 {
						response.Routing.FwMark = fmt.Sprintf("%d", ipsetConfig.Routing.FwMark)
					}
					if len(ipsetConfig.Routing.Interfaces) > 0 {
						response.Routing.Interface = ipsetConfig.Routing.Interfaces[0]
					}
					if ipsetConfig.Routing.DNSOverride != "" {
						response.Routing.DNSOverride = ipsetConfig.Routing.DNSOverride
					}
				}
				break
			}
		}
	}

	writeJSONData(w, response)
}

// findMatchingIPSets checks which IPSets contain the given host or IPs
func (h *Handler) findMatchingIPSets(cfg *config.Config, host string, ips []string) []IPSetMatch {
	matches := []IPSetMatch{}

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
						matches = append(matches, IPSetMatch{
							IPSetName: ipsetConfig.IPSetName,
							MatchType: "domain",
						})
						goto nextIPSet
					}
				}
			}

			// Check if any resolved IPs match (for URL/file lists, we can't check without reading them)
			// This would require reading the actual list files which is more complex
			// For now, we'll just mark domain matches
		}
	nextIPSet:
	}

	return matches
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

// CheckPing performs a ping to the given host.
// POST /api/v1/check/ping
func (h *Handler) CheckPing(w http.ResponseWriter, r *http.Request) {
	var req CheckRequest
	if err := decodeJSON(r, &req); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	if req.Host == "" {
		WriteInvalidRequest(w, "Host is required")
		return
	}

	response := PingCheckResponse{
		Host:        req.Host,
		PacketsSent: 4,
	}

	// Resolve IP if it's a domain
	ips, err := net.LookupHost(req.Host)
	if err == nil && len(ips) > 0 {
		response.ResolvedIP = ips[0]
	}

	// Run ping command (4 packets, 2 second timeout)
	cmd := exec.Command("ping", "-c", "4", "-W", "2", req.Host)
	output, err := cmd.CombinedOutput()
	response.Output = string(output)

	if err != nil {
		response.Success = false
		response.Error = fmt.Sprintf("Ping failed: %v", err)
		writeJSONData(w, response)
		return
	}

	response.Success = true

	// Parse ping output for statistics
	h.parsePingOutput(string(output), &response)

	writeJSONData(w, response)
}

// parsePingOutput extracts statistics from ping output
func (h *Handler) parsePingOutput(output string, response *PingCheckResponse) {
	// Example ping output:
	// 4 packets transmitted, 4 received, 0% packet loss, time 3004ms
	// rtt min/avg/max/mdev = 10.123/15.456/20.789/3.456 ms

	// Parse packet statistics
	packetRegex := regexp.MustCompile(`(\d+) packets transmitted, (\d+) received, ([\d.]+)% packet loss`)
	if matches := packetRegex.FindStringSubmatch(output); len(matches) == 4 {
		if sent, err := strconv.Atoi(matches[1]); err == nil {
			response.PacketsSent = sent
		}
		if recv, err := strconv.Atoi(matches[2]); err == nil {
			response.PacketsRecv = recv
		}
		if loss, err := strconv.ParseFloat(matches[3], 64); err == nil {
			response.PacketLoss = loss
		}
	}

	// Parse RTT statistics
	rttRegex := regexp.MustCompile(`rtt min/avg/max/[^ ]+ = ([\d.]+)/([\d.]+)/([\d.]+)/[\d.]+ ms`)
	if matches := rttRegex.FindStringSubmatch(output); len(matches) == 4 {
		if min, err := strconv.ParseFloat(matches[1], 64); err == nil {
			response.MinRTT = min
		}
		if avg, err := strconv.ParseFloat(matches[2], 64); err == nil {
			response.AvgRTT = avg
		}
		if max, err := strconv.ParseFloat(matches[3], 64); err == nil {
			response.MaxRTT = max
		}
	}
}

// CheckTraceroute performs a traceroute to the given host.
// POST /api/v1/check/traceroute
func (h *Handler) CheckTraceroute(w http.ResponseWriter, r *http.Request) {
	var req CheckRequest
	if err := decodeJSON(r, &req); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	if req.Host == "" {
		WriteInvalidRequest(w, "Host is required")
		return
	}

	response := TracerouteCheckResponse{
		Host: req.Host,
		Hops: []TracerouteHop{},
	}

	// Resolve IP if it's a domain
	ips, err := net.LookupHost(req.Host)
	if err == nil && len(ips) > 0 {
		response.ResolvedIP = ips[0]
	}

	// Run traceroute command (max 30 hops, 2 second timeout per hop)
	cmd := exec.Command("traceroute", "-m", "30", "-w", "2", req.Host)
	output, err := cmd.CombinedOutput()
	response.Output = string(output)

	if err != nil {
		response.Success = false
		response.Error = fmt.Sprintf("Traceroute failed: %v", err)
		writeJSONData(w, response)
		return
	}

	response.Success = true

	// Parse traceroute output
	h.parseTracerouteOutput(string(output), &response)

	writeJSONData(w, response)
}

// parseTracerouteOutput extracts hops from traceroute output
func (h *Handler) parseTracerouteOutput(output string, response *TracerouteCheckResponse) {
	// Example traceroute output:
	// 1  192.168.1.1 (192.168.1.1)  1.234 ms  1.123 ms  1.456 ms
	// 2  10.0.0.1 (10.0.0.1)  5.678 ms  5.234 ms  5.890 ms

	lines := strings.Split(output, "\n")
	hopRegex := regexp.MustCompile(`^\s*(\d+)\s+([^\s]+)\s+\(([^)]+)\)\s+([\d.]+)\s+ms`)

	for _, line := range lines {
		if matches := hopRegex.FindStringSubmatch(line); len(matches) >= 5 {
			hop := TracerouteHop{}

			if hopNum, err := strconv.Atoi(matches[1]); err == nil {
				hop.Hop = hopNum
			}

			hop.Hostname = matches[2]
			hop.IP = matches[3]

			if rtt, err := strconv.ParseFloat(matches[4], 64); err == nil {
				hop.RTT = rtt
			}

			response.Hops = append(response.Hops, hop)
		}
	}
}
