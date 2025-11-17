package api

import (
	"bufio"
	"net/http"
	"net/netip"
	"os"
	"strings"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// GetLists returns all lists in the configuration.
// GET /api/v1/lists
func (h *Handler) GetLists(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Convert to ListInfo with statistics
	listInfos := make([]*ListInfo, 0, len(cfg.Lists))
	for _, list := range cfg.Lists {
		listInfo := h.convertToListInfo(list, cfg)
		listInfos = append(listInfos, listInfo)
	}

	writeJSONData(w, ListsResponse{Lists: listInfos})
}

// GetList returns a specific list by name.
// GET /api/v1/lists/{name}
func (h *Handler) GetList(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find the list
	for _, list := range cfg.Lists {
		if list.ListName == name {
			listInfo := h.convertToListInfo(list, cfg)
			writeJSONData(w, listInfo)
			return
		}
	}

	WriteNotFound(w, "List")
}

// CreateList creates a new list.
// POST /api/v1/lists
func (h *Handler) CreateList(w http.ResponseWriter, r *http.Request) {
	var list config.ListSource
	if err := decodeJSON(r, &list); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate list has a name
	if list.ListName == "" {
		WriteInvalidRequest(w, "list_name is required")
		return
	}

	// Validate exactly one source type
	sourceCount := 0
	if list.URL != "" {
		sourceCount++
	}
	if list.File != "" {
		sourceCount++
	}
	if list.Hosts != nil {
		sourceCount++
	}
	if sourceCount != 1 {
		WriteInvalidRequest(w, "Exactly one of url, file, or hosts must be specified")
		return
	}

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Check if list already exists
	for _, existingList := range cfg.Lists {
		if existingList.ListName == list.ListName {
			WriteConflict(w, "List with name '"+list.ListName+"' already exists")
			return
		}
	}

	// Add the list
	cfg.Lists = append(cfg.Lists, &list)

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

	writeCreated(w, list)
}

// UpdateList updates an existing list.
// PUT /api/v1/lists/{name}
func (h *Handler) UpdateList(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	var updatedList config.ListSource
	if err := decodeJSON(r, &updatedList); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate list has a name
	if updatedList.ListName == "" {
		WriteInvalidRequest(w, "list_name is required")
		return
	}

	// Validate exactly one source type
	sourceCount := 0
	if updatedList.URL != "" {
		sourceCount++
	}
	if updatedList.File != "" {
		sourceCount++
	}
	if updatedList.Hosts != nil {
		sourceCount++
	}
	if sourceCount != 1 {
		WriteInvalidRequest(w, "Exactly one of url, file, or hosts must be specified")
		return
	}

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find and update the list
	found := false
	for i, list := range cfg.Lists {
		if list.ListName == name {
			// Check if renaming to an existing name
			if updatedList.ListName != name {
				for _, existingList := range cfg.Lists {
					if existingList.ListName == updatedList.ListName {
						WriteConflict(w, "List with name '"+updatedList.ListName+"' already exists")
						return
					}
				}
			}

			cfg.Lists[i] = &updatedList
			found = true
			break
		}
	}

	if !found {
		WriteNotFound(w, "List")
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

	writeJSONData(w, updatedList)
}

// DeleteList deletes a list.
// DELETE /api/v1/lists/{name}
func (h *Handler) DeleteList(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find and remove the list
	found := false
	for i, list := range cfg.Lists {
		if list.ListName == name {
			// Check if list is referenced by any ipset
			for _, ipset := range cfg.IPSets {
				for _, listRef := range ipset.Lists {
					if listRef == name {
						WriteConflict(w, "List is referenced by ipset '"+ipset.IPSetName+"'")
						return
					}
				}
			}

			cfg.Lists = append(cfg.Lists[:i], cfg.Lists[i+1:]...)
			found = true
			break
		}
	}

	if !found {
		WriteNotFound(w, "List")
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

// convertToListInfo converts a config.ListSource to ListInfo with statistics.
func (h *Handler) convertToListInfo(list *config.ListSource, cfg *config.Config) *ListInfo {
	info := &ListInfo{
		ListName: list.ListName,
		Type:     list.Type(),
		URL:      list.URL,
		File:     list.File,
	}

	// Calculate statistics
	stats := ListStatistics{}

	// For inline hosts, count directly
	if list.Hosts != nil {
		stats.TotalHosts = len(list.Hosts)
	} else {
		// For file and URL-based lists, try to read the file
		filePath, err := list.GetAbsolutePath(cfg)
		if err == nil {
			stats = h.calculateFileStats(filePath)

			// For URL-based lists, add download status
			if list.URL != "" {
				fileInfo, err := os.Stat(filePath)
				if err == nil {
					stats.Downloaded = true
					stats.LastModified = fileInfo.ModTime().Format(time.RFC3339)
				} else {
					stats.Downloaded = false
				}
			}
		}
	}

	info.Stats = stats
	return info
}

// calculateFileStats reads a file and calculates statistics.
func (h *Handler) calculateFileStats(filePath string) ListStatistics {
	stats := ListStatistics{}

	file, err := os.Open(filePath)
	if err != nil {
		return stats
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Try to parse as CIDR
		if prefix, err := netip.ParsePrefix(line); err == nil {
			if prefix.Addr().Is4() {
				stats.IPv4Subnets++
			} else if prefix.Addr().Is6() {
				stats.IPv6Subnets++
			}
		} else if addr, err := netip.ParseAddr(line); err == nil {
			// Single IP address
			if addr.Is4() {
				stats.IPv4Subnets++
			} else if addr.Is6() {
				stats.IPv6Subnets++
			}
		} else {
			// Assume it's a domain/host
			stats.TotalHosts++
		}
	}

	return stats
}
