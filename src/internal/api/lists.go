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
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// GetLists returns all lists in the configuration.
// GET /api/v1/lists
func (h *Handler) GetLists(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Convert to ListInfo with statistics (don't include hosts array for GET all)
	listInfos := make([]*ListInfo, 0, len(cfg.Lists))
	for _, list := range cfg.Lists {
		listInfo := h.convertToListInfo(list, cfg, false)
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
			listInfo := h.convertToListInfo(list, cfg, true)
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

	// Auto-remove empty lines from hosts
	if list.Hosts != nil {
		filtered := make([]string, 0, len(list.Hosts))
		for _, host := range list.Hosts {
			if trimmed := strings.TrimSpace(host); trimmed != "" {
				filtered = append(filtered, trimmed)
			}
		}
		list.Hosts = filtered
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

	// Invalidate cache for the new list
	h.deps.ListManager().InvalidateCache(&list, cfg)

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

	// Prevent renaming - list_name must match the URL parameter
	if updatedList.ListName != name {
		WriteInvalidRequest(w, "Cannot rename list. list_name must match the URL parameter '"+name+"'")
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

	// Auto-remove empty lines from hosts
	if updatedList.Hosts != nil {
		filtered := make([]string, 0, len(updatedList.Hosts))
		for _, host := range updatedList.Hosts {
			if trimmed := strings.TrimSpace(host); trimmed != "" {
				filtered = append(filtered, trimmed)
			}
		}
		updatedList.Hosts = filtered
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

	// Invalidate cache for the updated list
	h.deps.ListManager().InvalidateCache(&updatedList, cfg)

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
	var deletedList *config.ListSource
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

			deletedList = list
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

	// Invalidate cache for the deleted list
	if deletedList != nil {
		h.deps.ListManager().InvalidateCache(deletedList, cfg)
	}

	writeNoContent(w)
}

// convertToListInfo converts a config.ListSource to ListInfo with statistics.
// For file and URL-based lists, parses the file to calculate statistics on-demand.
// For inline hosts, counts directly from the hosts array.
// If includeHosts is true, includes the hosts array for inline hosts lists.
func (h *Handler) convertToListInfo(list *config.ListSource, cfg *config.Config, includeHosts bool) *ListInfo {
	info := &ListInfo{
		ListName: list.ListName,
		Type:     list.Type(),
		URL:      list.URL,
		File:     list.File,
	}

	// Include hosts array only for GET single list endpoint
	if includeHosts && list.Hosts != nil {
		info.Hosts = list.Hosts
	}

	// Build statistics object
	stats := &ListStatistics{}

	// Calculate statistics based on list type
	if list.Hosts != nil {
		// For inline hosts, count directly
		totalHosts, ipv4Count, ipv6Count := h.calculateInlineStatistics(list.Hosts)
		stats.TotalHosts = &totalHosts
		stats.IPv4Subnets = &ipv4Count
		stats.IPv6Subnets = &ipv6Count
	} else if list.URL != "" || list.File != "" {
		// For file and URL-based lists, parse the file if it exists
		downloaded, lastModified := h.getFileStats(list, cfg)

		if list.URL != "" {
			stats.Downloaded = downloaded
			if downloaded {
				lastModifiedStr := lastModified.Format(time.RFC3339)
				stats.LastModified = &lastModifiedStr
			}
		}

		if downloaded {
			// Parse the file to calculate statistics
			totalHosts, ipv4Count, ipv6Count := h.calculateFileStatistics(list, cfg)
			stats.TotalHosts = &totalHosts
			stats.IPv4Subnets = &ipv4Count
			stats.IPv6Subnets = &ipv6Count
		}
	}

	info.Stats = stats
	return info
}

// getFileStats returns file statistics for a list.
// For URL-based lists, returns (true, modTime) if file exists.
// For file-based lists, returns (true, modTime) if file exists.
// For inline hosts, returns (false, zero time).
func (h *Handler) getFileStats(list *config.ListSource, cfg *config.Config) (bool, time.Time) {
	if list.Hosts != nil {
		// Inline hosts have no file
		return false, time.Time{}
	}

	if list.URL != "" || list.File != "" {
		listPath, err := list.GetAbsolutePathAndCheckExists(cfg)
		if err != nil {
			// File doesn't exist yet (not downloaded)
			return false, time.Time{}
		}

		fileInfo, err := os.Stat(listPath)
		if err != nil {
			return false, time.Time{}
		}

		return true, fileInfo.ModTime()
	}

	return false, time.Time{}
}

// calculateInlineStatistics counts domains, IPv4, and IPv6 entries in an inline hosts array.
// Returns (totalHosts, ipv4Count, ipv6Count).
func (h *Handler) calculateInlineStatistics(hosts []string) (int, int, int) {
	totalHosts := 0
	ipv4Count := 0
	ipv6Count := 0

	for _, host := range hosts {
		line := strings.TrimSpace(host)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Check if it's a domain name
		if utils.IsDNSName(line) {
			totalHosts++
			continue
		}

		// Try to parse as IP/CIDR
		cidr := line
		if !strings.Contains(line, "/") {
			cidr = line + "/32"
		}

		if netPrefix, err := netip.ParsePrefix(cidr); err == nil && netPrefix.IsValid() {
			totalHosts++
			if netPrefix.Addr().Is4() {
				ipv4Count++
			} else if netPrefix.Addr().Is6() {
				ipv6Count++
			}
		}
	}

	return totalHosts, ipv4Count, ipv6Count
}

// calculateFileStatistics parses a list file and counts domains, IPv4, and IPv6 entries.
// Returns (totalHosts, ipv4Count, ipv6Count).
func (h *Handler) calculateFileStatistics(list *config.ListSource, cfg *config.Config) (int, int, int) {
	totalHosts := 0
	ipv4Count := 0
	ipv6Count := 0

	listPath, err := list.GetAbsolutePathAndCheckExists(cfg)
	if err != nil {
		return 0, 0, 0
	}

	file, err := os.Open(listPath)
	if err != nil {
		return 0, 0, 0
	}
	defer utils.CloseOrWarn(file)

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Check if it's a domain name
		if utils.IsDNSName(line) {
			totalHosts++
			continue
		}

		// Try to parse as IP/CIDR
		cidr := line
		if !strings.Contains(line, "/") {
			cidr = line + "/32"
		}

		if netPrefix, err := netip.ParsePrefix(cidr); err == nil && netPrefix.IsValid() {
			totalHosts++
			if netPrefix.Addr().Is4() {
				ipv4Count++
			} else if netPrefix.Addr().Is6() {
				ipv6Count++
			}
		}
	}

	return totalHosts, ipv4Count, ipv6Count
}

// DownloadList downloads a specific list from its URL.
// POST /api/v1/lists-download/:name
func (h *Handler) DownloadList(w http.ResponseWriter, r *http.Request) {
	name := chi.URLParam(r, "name")

	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Find the list
	var list *config.ListSource
	for _, l := range cfg.Lists {
		if l.ListName == name {
			list = l
			break
		}
	}

	if list == nil {
		WriteNotFound(w, "List")
		return
	}

	// Check if list has a URL
	if list.URL == "" {
		WriteInvalidRequest(w, "List does not have a URL configured")
		return
	}

	// Download the list
	changed, err := lists.DownloadList(list, cfg)
	if err != nil {
		WriteInternalError(w, "Failed to download list: "+err.Error())
		return
	}

	// Invalidate cache for this list
	h.deps.ListManager().InvalidateCache(list, cfg)

	// Return the updated list info with changed status
	listInfo := h.convertToListInfo(list, cfg, false)
	response := &ListDownloadResponse{
		ListInfo: listInfo,
		Changed:  changed,
	}
	writeJSONData(w, response)
}

// DownloadAllLists force re-downloads all lists from their URLs.
// POST /api/v1/lists-download
func (h *Handler) DownloadAllLists(w http.ResponseWriter, r *http.Request) {
	cfg, err := h.loadConfig()
	if err != nil {
		WriteInternalError(w, "Failed to load configuration: "+err.Error())
		return
	}

	// Force download all lists (re-download even if they exist)
	if err := lists.DownloadListsIfUpdated(cfg); err != nil {
		WriteInternalError(w, "Failed to download lists: "+err.Error())
		return
	}

	// Invalidate all caches
	h.deps.ListManager().InvalidateAll()

	// Return success message
	writeJSONData(w, map[string]string{
		"message": "All lists downloaded successfully",
	})
}
