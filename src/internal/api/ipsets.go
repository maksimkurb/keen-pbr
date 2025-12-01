package api

import (
	"errors"
	"fmt"
	"net/http"
	"strings"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// writeIPSetValidationError filters and formats validation errors for a specific ipset.
func writeIPSetValidationError(w http.ResponseWriter, err error, ipsetName string, index int) {
	var ve config.ValidationErrors
	if errors.As(err, &ve) {
		var newErrs config.ValidationErrors

		expectedItemName := ipsetName
		if expectedItemName == "" {
			expectedItemName = fmt.Sprintf("ipset[%d]", index)
		}

		for _, e := range ve {
			if e.ItemName == expectedItemName {
				// Fix FieldPath: remove "ipset.N." prefix
				// ipset.0.ipset_name -> [ipset, 0, ipset_name]
				parts := strings.SplitN(e.FieldPath, ".", 3)
				if len(parts) >= 3 && parts[0] == "ipset" {
					e.FieldPath = parts[2]
				}
				newErrs = append(newErrs, e)
			}
		}

		if len(newErrs) > 0 {
			WriteValidationError(w, newErrs)
			return
		}
	}

	// Fallback to original error if no specific errors found or not a ValidationErrors type
	WriteValidationError(w, err)
}

// findLowestAvailablePriority finds the lowest available priority in the range 500-1000
// that is not already used by any existing ipset.
func findLowestAvailablePriority(ipsets []*config.IPSetConfig) int {
	// Create a map of used priorities
	usedPriorities := make(map[int]bool)
	for _, ipset := range ipsets {
		if ipset.Routing != nil {
			usedPriorities[ipset.Routing.IPRulePriority] = true
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

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Auto-assign priority/table/fwmark if not provided (or if they're 0)
	if ipset.Routing != nil && (ipset.Routing.IPRulePriority == 0 || ipset.Routing.IPRouteTable == 0 || ipset.Routing.FwMark == 0) {
		// Find the lowest available priority in range 500-1000
		priority := findLowestAvailablePriority(cfg.IPSets)
		if ipset.Routing.IPRulePriority == 0 {
			ipset.Routing.IPRulePriority = priority
		}
		if ipset.Routing.IPRouteTable == 0 {
			ipset.Routing.IPRouteTable = priority
		}
		if ipset.Routing.FwMark == 0 {
			ipset.Routing.FwMark = uint32(priority)
		}
	}

	// Add the ipset
	cfg.IPSets = append(cfg.IPSets, &ipset)
	index := len(cfg.IPSets) - 1

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		writeIPSetValidationError(w, err, ipset.IPSetName, index)
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

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find and update the ipset
	found := false
	index := -1
	for i, ipset := range cfg.IPSets {
		if ipset.IPSetName == name {
			cfg.IPSets[i] = &updatedIPSet
			found = true
			index = i
			break
		}
	}

	if !found {
		WriteNotFound(w, "IPSet")
		return
	}

	// Validate configuration
	if err := h.validateConfig(cfg); err != nil {
		writeIPSetValidationError(w, err, updatedIPSet.IPSetName, index)
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
		WriteValidationError(w, err)
		return
	}

	// Save configuration
	if err := h.saveConfig(cfg); err != nil {
		WriteInternalError(w, "Failed to save configuration: "+err.Error())
		return
	}

	writeNoContent(w)
}
