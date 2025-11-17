package lists

import (
	"bufio"
	"net/netip"
	"os"
	"strings"
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

// cacheEntry stores cached statistics with metadata.
type cacheEntry struct {
	stats      ListStatistics
	cachedAt   time.Time
	fileModTime time.Time
}

// Manager manages list statistics with caching.
type Manager struct {
	cache     map[string]*cacheEntry
	cacheTTL  time.Duration
	mu        sync.RWMutex
}

// NewManager creates a new list manager with default cache TTL of 5 minutes.
func NewManager() *Manager {
	return NewManagerWithTTL(5 * time.Minute)
}

// NewManagerWithTTL creates a new list manager with custom cache TTL.
func NewManagerWithTTL(ttl time.Duration) *Manager {
	return &Manager{
		cache:    make(map[string]*cacheEntry),
		cacheTTL: ttl,
	}
}

// GetStatistics returns statistics for a list, using cache if available.
func (m *Manager) GetStatistics(list *config.ListSource, cfg *config.Config) ListStatistics {
	// For inline hosts, return immediately (no caching needed)
	if list.Hosts != nil {
		return ListStatistics{
			TotalHosts:   len(list.Hosts),
			calculatedAt: time.Now(),
		}
	}

	// Get file path
	filePath, err := list.GetAbsolutePath(cfg)
	if err != nil {
		return ListStatistics{}
	}

	// Check cache
	m.mu.RLock()
	entry, exists := m.cache[filePath]
	m.mu.RUnlock()

	// Get file info
	fileInfo, err := os.Stat(filePath)

	// For URL-based lists, track download status
	downloaded := err == nil
	var fileModTime time.Time
	if downloaded {
		fileModTime = fileInfo.ModTime()
	}

	// Return cached stats if valid
	if exists && m.isCacheValid(entry, fileModTime, downloaded) {
		stats := entry.stats
		stats.Downloaded = downloaded
		if downloaded {
			stats.LastModified = fileModTime
		}
		return stats
	}

	// Calculate new statistics
	stats := m.calculateFileStats(filePath)
	stats.Downloaded = downloaded
	if downloaded {
		stats.LastModified = fileModTime
	}
	stats.calculatedAt = time.Now()

	// Cache the result (only if file exists and was successfully read)
	if downloaded && (stats.TotalHosts > 0 || stats.IPv4Subnets > 0 || stats.IPv6Subnets > 0) {
		m.mu.Lock()
		m.cache[filePath] = &cacheEntry{
			stats:       stats,
			cachedAt:    time.Now(),
			fileModTime: fileModTime,
		}
		m.mu.Unlock()
	}

	return stats
}

// isCacheValid checks if cached entry is still valid.
func (m *Manager) isCacheValid(entry *cacheEntry, currentModTime time.Time, fileExists bool) bool {
	// Cache invalid if TTL expired
	if time.Since(entry.cachedAt) > m.cacheTTL {
		return false
	}

	// Cache invalid if file doesn't exist anymore
	if !fileExists {
		return false
	}

	// Cache invalid if file was modified
	if !currentModTime.Equal(entry.fileModTime) {
		return false
	}

	return true
}

// InvalidateCache invalidates the cache for a specific list.
func (m *Manager) InvalidateCache(list *config.ListSource, cfg *config.Config) {
	if list.Hosts != nil {
		return // No cache for inline hosts
	}

	filePath, err := list.GetAbsolutePath(cfg)
	if err != nil {
		return
	}

	m.mu.Lock()
	delete(m.cache, filePath)
	m.mu.Unlock()
}

// InvalidateAll invalidates all cached statistics.
func (m *Manager) InvalidateAll() {
	m.mu.Lock()
	m.cache = make(map[string]*cacheEntry)
	m.mu.Unlock()
}

// calculateFileStats reads a file and calculates statistics.
func (m *Manager) calculateFileStats(filePath string) ListStatistics {
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

// GetCacheStats returns cache statistics for monitoring.
func (m *Manager) GetCacheStats() map[string]interface{} {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return map[string]interface{}{
		"cached_entries": len(m.cache),
		"cache_ttl":      m.cacheTTL.String(),
	}
}
