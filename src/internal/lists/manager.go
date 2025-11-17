package lists

import (
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// ListStatistics contains statistics about a list's content.
type ListStatistics struct {
	TotalHosts   int
	IPv4Subnets  int
	IPv6Subnets  int
	Downloaded   bool
	LastModified time.Time
	calculatedAt time.Time
}

// Manager manages list statistics with caching.
// Statistics are populated when lists are processed for dnsmasq/ipsets,
// not on-demand, to avoid duplicate file parsing.
type Manager struct {
	cache map[string]ListStatistics
	mu    sync.RWMutex
}

// NewManager creates a new list manager.
func NewManager() *Manager {
	return &Manager{
		cache: make(map[string]ListStatistics),
	}
}

// GetStatistics returns cached statistics for a list.
// Statistics are calculated during list processing (dnsmasq/ipsets),
// not on-demand. Returns empty stats if not yet calculated.
func (m *Manager) GetStatistics(list *config.ListSource, cfg *config.Config) ListStatistics {
	// For inline hosts, count directly (no file processing needed)
	if list.Hosts != nil {
		return ListStatistics{
			TotalHosts:   len(list.Hosts),
			calculatedAt: time.Now(),
		}
	}

	// Get cache key
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return ListStatistics{}
	}

	// Return cached stats
	m.mu.RLock()
	defer m.mu.RUnlock()

	if stats, exists := m.cache[cacheKey]; exists {
		return stats
	}

	// No stats yet - will be calculated during list processing
	return ListStatistics{}
}

// RecordLineProcessed should be called when a line is processed from a list.
// This incrementally builds statistics during normal list operations.
func (m *Manager) RecordLineProcessed(list *config.ListSource, cfg *config.Config, line string, isDomain bool, isIPv4 bool, isIPv6 bool) {
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	stats, exists := m.cache[cacheKey]
	if !exists {
		stats = ListStatistics{
			calculatedAt: time.Now(),
		}
	}

	if isDomain {
		stats.TotalHosts++
	} else if isIPv4 {
		stats.IPv4Subnets++
	} else if isIPv6 {
		stats.IPv6Subnets++
	}

	m.cache[cacheKey] = stats
}

// StartListProcessing marks that a list is being processed.
// This should be called before iterating over a list.
func (m *Manager) StartListProcessing(list *config.ListSource, cfg *config.Config) {
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	// Initialize with empty stats
	m.cache[cacheKey] = ListStatistics{
		calculatedAt: time.Now(),
	}
}

// FinishListProcessing marks that a list processing is complete.
// This updates download status for URL-based lists.
func (m *Manager) FinishListProcessing(list *config.ListSource, cfg *config.Config, downloaded bool, lastModified time.Time) {
	if list.URL == "" {
		return // Only for URL-based lists
	}

	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if stats, exists := m.cache[cacheKey]; exists {
		stats.Downloaded = downloaded
		if downloaded {
			stats.LastModified = lastModified
		}
		m.cache[cacheKey] = stats
	}
}

// getCacheKey returns a consistent cache key for a list.
func (m *Manager) getCacheKey(list *config.ListSource, cfg *config.Config) string {
	// For file and URL lists, use absolute path as key
	if list.URL != "" || list.File != "" {
		if path, err := list.GetAbsolutePath(cfg); err == nil {
			return path
		}
	}
	return ""
}

// InvalidateCache invalidates the cache for a specific list.
// This should be called when a list is modified or downloaded via API.
func (m *Manager) InvalidateCache(list *config.ListSource, cfg *config.Config) {
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.cache, cacheKey)
}

// InvalidateAll invalidates all cached statistics.
// This should be called after downloading all lists.
func (m *Manager) InvalidateAll() {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.cache = make(map[string]ListStatistics)
}

// GetCacheStats returns cache statistics for monitoring.
func (m *Manager) GetCacheStats() map[string]interface{} {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return map[string]interface{}{
		"cached_lists": len(m.cache),
	}
}
