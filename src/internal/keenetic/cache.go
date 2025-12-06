package keenetic

import (
	"sync"
)

// Cache manages cached data with thread-safe access.
//
// The cache stores version information to avoid repeated API calls.
// All methods are safe for concurrent use.
type Cache struct {
	mu      sync.RWMutex
	version *KeeneticVersion
}

// NewCache creates a new cache instance with the specified TTL.
//
// If ttl is 0, cached values never expire.
func NewCache() *Cache {
	return &Cache{}
}

// GetVersion retrieves the cached Keenetic version.
//
// Returns the cached version and true if found, nil and false otherwise.
func (c *Cache) GetVersion() (*KeeneticVersion, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if c.version != nil {
		return c.version, true
	}
	return nil, false
}

// SetVersion stores the Keenetic version in the cache.
func (c *Cache) SetVersion(v *KeeneticVersion) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.version = v
}

// Clear removes all cached data.
func (c *Cache) Clear() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.version = nil
}
