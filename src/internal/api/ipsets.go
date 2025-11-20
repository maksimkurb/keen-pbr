package api

import (
	"net/http"
	"regexp"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

var ipsetNamePattern = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)

// findLowestAvailablePriority finds the lowest available priority in the range 500-1000
// that is not already used by any existing ipset.
func findLowestAvailablePriority(ipsets []*config.IPSetConfig) int {
	// Create a map of used priorities
	usedPriorities := make(map[int]bool)
	for _, ipset := range ipsets {
		if ipset.Routing != nil {
			usedPriorities[ipset.Routing.IpRulePriority] = true
		}
	}

	// Find the first available priority in range 500-1000
	for priority := 500; priority <= 1000; priority++ {
		if !usedPriorities[priority] {
			return priority
		}
	}

	// If all priorities 500-1000 are taken, return 1000 (edge case)
	return 1000
}

// GetIPSets returns all ipsets in the configuration.
// GET /api/v1/ipsets
func (h *Handler) GetIPSets(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	writeJSONData(w, IPSetsResponse{IPSets: cfg.IPSets})
}

// GetIPSet returns a specific ipset by name.
// GET /api/v1/ipsets/{name}
func (h *Handler) GetIPSet(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find the ipset
	for _, ipset := range cfg.IPSets {
		if ipset.IPSetName == name {
			writeJSONData(w, ipset)
			return
		}
	}

	WriteNotFound(w, "IPSet")
}

// CreateIPSet creates a new ipset.
// POST /api/v1/ipsets
func (h *Handler) CreateIPSet(w http.ResponseWriter, r *http.Request) {
	var ipset config.IPSetConfig
	if err := decodeJSON(r, &ipset); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate ipset name
	if ipset.IPSetName == "" {
		WriteInvalidRequest(w, "ipset_name is required")
		return
	}

	if !ipsetNamePattern.MatchString(ipset.IPSetName) {
		WriteInvalidRequest(w, "ipset_name must match pattern ^[a-z][a-z0-9_]*$")
		return
	}

	// Validate IP version
	if ipset.IPVersion != 4 && ipset.IPVersion != 6 {
		WriteInvalidRequest(w, "ip_version must be 4 or 6")
		return
	}

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Check if ipset already exists
	for _, existingIPSet := range cfg.IPSets {
		if existingIPSet.IPSetName == ipset.IPSetName {
			WriteConflict(w, "IPSet with name '"+ipset.IPSetName+"' already exists")
			return
		}
	}

	// Validate list references
	for _, listName := range ipset.Lists {
		found := false
		for _, list := range cfg.Lists {
			if list.ListName == listName {
				found = true
				break
			}
		}
		if !found {
			WriteValidationError(w, "Referenced list '"+listName+"' does not exist", map[string]interface{}{
				"list_name": listName,
			})
			return
		}
	}

	// Auto-assign priority/table/fwmark if not provided (or if they're 0)
	if ipset.Routing != nil && (ipset.Routing.IpRulePriority == 0 || ipset.Routing.IpRouteTable == 0 || ipset.Routing.FwMark == 0) {
		// Find the lowest available priority in range 500-1000
		priority := findLowestAvailablePriority(cfg.IPSets)
		if ipset.Routing.IpRulePriority == 0 {
			ipset.Routing.IpRulePriority = priority
		}
		if ipset.Routing.IpRouteTable == 0 {
			ipset.Routing.IpRouteTable = priority
		}
		if ipset.Routing.FwMark == 0 {
			ipset.Routing.FwMark = uint32(priority)
		}
	}

	// Add the ipset
	cfg.IPSets = append(cfg.IPSets, &ipset)

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		WriteValidationError(w, "Configuration validation failed: "+err.Error(), nil)
		return
	}

	// Save configuration
	if err := h.saveConfig(cfg); err != nil {
		WriteInternalError(w, "Failed to save configuration: "+err.Error())
		return
	}

	writeCreated(w, ipset)
}

// UpdateIPSet updates an existing ipset.
// PUT /api/v1/ipsets/{name}
func (h *Handler) UpdateIPSet(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	var updatedIPSet config.IPSetConfig
	if err := decodeJSON(r, &updatedIPSet); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate ipset name
	if updatedIPSet.IPSetName == "" {
		WriteInvalidRequest(w, "ipset_name is required")
		return
	}

	if !ipsetNamePattern.MatchString(updatedIPSet.IPSetName) {
		WriteInvalidRequest(w, "ipset_name must match pattern ^[a-z][a-z0-9_]*$")
		return
	}

	// Validate IP version
	if updatedIPSet.IPVersion != 4 && updatedIPSet.IPVersion != 6 {
		WriteInvalidRequest(w, "ip_version must be 4 or 6")
		return
	}

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find and update the ipset
	found := false
	for i, ipset := range cfg.IPSets {
		if ipset.IPSetName == name {
			// Check if renaming to an existing name
			if updatedIPSet.IPSetName != name {
				for _, existingIPSet := range cfg.IPSets {
					if existingIPSet.IPSetName == updatedIPSet.IPSetName {
						WriteConflict(w, "IPSet with name '"+updatedIPSet.IPSetName+"' already exists")
						return
					}
				}
			}

			// Validate list references
			for _, listName := range updatedIPSet.Lists {
				listFound := false
				for _, list := range cfg.Lists {
					if list.ListName == listName {
						listFound = true
						break
					}
				}
				if !listFound {
					WriteValidationError(w, "Referenced list '"+listName+"' does not exist", map[string]interface{}{
						"list_name": listName,
					})
					return
				}
			}

			cfg.IPSets[i] = &updatedIPSet
			found = true
			break
		}
	}

	if !found {
		WriteNotFound(w, "IPSet")
		return
	}

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		WriteValidationError(w, "Configuration validation failed: "+err.Error(), nil)
		return
	}

	// Save configuration
	if err := h.saveConfig(cfg); err != nil {
		WriteInternalError(w, "Failed to save configuration: "+err.Error())
		return
	}

	writeJSONData(w, updatedIPSet)
}

// DeleteIPSet deletes an ipset.
// DELETE /api/v1/ipsets/{name}
func (h *Handler) DeleteIPSet(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find and remove the ipset
	found := false
	for i, ipset := range cfg.IPSets {
		if ipset.IPSetName == name {
			cfg.IPSets = append(cfg.IPSets[:i], cfg.IPSets[i+1:]...)
			found = true
			break
		}
	}

	if !found {
		WriteNotFound(w, "IPSet")
		return
	}

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		WriteValidationError(w, "Configuration validation failed: "+err.Error(), nil)
		return
	}

	// Save configuration
	if err := h.saveConfig(cfg); err != nil {
		WriteInternalError(w, "Failed to save configuration: "+err.Error())
		return
	}

	writeNoContent(w)
}
