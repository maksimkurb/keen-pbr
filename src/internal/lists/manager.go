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
// not on-demand. Returns nil if not yet calculated.
func (m *Manager) GetStatistics(list *config.ListSource, cfg *config.Config) *ListStatistics {
	// For inline hosts, count directly (no file processing needed)
	if list.Hosts != nil {
		return &ListStatistics{
			TotalHosts:   len(list.Hosts),
			calculatedAt: time.Now(),
		}
	}

	// Get cache key
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return nil
	}

	// Return cached stats
	m.mu.RLock()
	defer m.mu.RUnlock()

	if stats, exists := m.cache[cacheKey]; exists {
		return &stats
	}

	// No stats yet - will be calculated during list processing
	return nil
}

// UpdateStatistics updates the cached statistics for a list.
// This should be called after list processing is complete with the final counts.
// Note: Download status and last modified date are not cached - they are
// determined on-demand from file stats since they don't require parsing.
func (m *Manager) UpdateStatistics(list *config.ListSource, cfg *config.Config, totalHosts int, ipv4Subnets int, ipv6Subnets int) {
	cacheKey := m.getCacheKey(list, cfg)
	if cacheKey == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	m.cache[cacheKey] = ListStatistics{
		TotalHosts:   totalHosts,
		IPv4Subnets:  ipv4Subnets,
		IPv6Subnets:  ipv6Subnets,
		calculatedAt: time.Now(),
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
