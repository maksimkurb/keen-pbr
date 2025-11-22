package dnsproxy

import (
	"net"
	"sync"
	"time"
)

// CachedAddress represents a cached IP address with its expiration time.
type CachedAddress struct {
	Address  net.IP
	Deadline time.Time
}

// RecordsCache stores DNS records (addresses and CNAME aliases) with TTL-based expiration.
// It tracks the relationship between domain names and their resolved addresses,
// including CNAME chain resolution.
type RecordsCache struct {
	mu sync.RWMutex

	// addresses maps domain name -> list of cached addresses
	addresses map[string][]CachedAddress

	// aliases maps domain name -> target domain name (CNAME)
	// This allows us to track: subdomain.example.com -> cdn.example.com -> cdn.cloudflare.com
	aliases map[string]aliasEntry
}

type aliasEntry struct {
	Target   string
	Deadline time.Time
}

// NewRecordsCache creates a new records cache.
func NewRecordsCache() *RecordsCache {
	return &RecordsCache{
		addresses: make(map[string][]CachedAddress),
		aliases:   make(map[string]aliasEntry),
	}
}

// AddAddress adds an IP address for a domain with the specified TTL.
func (r *RecordsCache) AddAddress(domain string, address net.IP, ttl uint32) {
	r.mu.Lock()
	defer r.mu.Unlock()

	deadline := time.Now().Add(time.Duration(ttl) * time.Second)

	// Check if address already exists
	if addrs, exists := r.addresses[domain]; exists {
		for i, addr := range addrs {
			if addr.Address.Equal(address) {
				// Update deadline if new one is later
				if deadline.After(addr.Deadline) {
					r.addresses[domain][i].Deadline = deadline
				}
				return
			}
		}
	}

	// Add new address
	r.addresses[domain] = append(r.addresses[domain], CachedAddress{
		Address:  address,
		Deadline: deadline,
	})
}

// AddAlias adds a CNAME alias (domain -> target) with the specified TTL.
func (r *RecordsCache) AddAlias(domain, target string, ttl uint32) {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.aliases[domain] = aliasEntry{
		Target:   target,
		Deadline: time.Now().Add(time.Duration(ttl) * time.Second),
	}
}

// GetAddresses returns all non-expired addresses for a domain.
func (r *RecordsCache) GetAddresses(domain string) []CachedAddress {
	r.mu.RLock()
	defer r.mu.RUnlock()

	now := time.Now()
	var result []CachedAddress

	if addrs, exists := r.addresses[domain]; exists {
		for _, addr := range addrs {
			if addr.Deadline.After(now) {
				result = append(result, addr)
			}
		}
	}

	return result
}

// GetAliases returns all domain names that resolve to the given domain,
// including the domain itself and all CNAMEs in the chain.
// For example, if we have:
//
//	subdomain.example.com -> cdn.example.com -> cdn.cloudflare.com
//
// Calling GetAliases("cdn.cloudflare.com") returns:
//
//	["cdn.cloudflare.com", "cdn.example.com", "subdomain.example.com"]
func (r *RecordsCache) GetAliases(domain string) []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	now := time.Now()
	result := []string{domain}

	// Build reverse alias map
	reverseAliases := make(map[string][]string)
	for source, entry := range r.aliases {
		if entry.Deadline.After(now) {
			reverseAliases[entry.Target] = append(reverseAliases[entry.Target], source)
		}
	}

	// BFS to find all domains that resolve to this domain
	visited := make(map[string]bool)
	visited[domain] = true
	queue := []string{domain}

	for len(queue) > 0 {
		current := queue[0]
		queue = queue[1:]

		if sources, exists := reverseAliases[current]; exists {
			for _, source := range sources {
				if !visited[source] {
					visited[source] = true
					result = append(result, source)
					queue = append(queue, source)
				}
			}
		}
	}

	return result
}

// GetTargetChain returns the CNAME chain for a domain.
// For example, if we have:
//
//	subdomain.example.com -> cdn.example.com -> cdn.cloudflare.com
//
// Calling GetTargetChain("subdomain.example.com") returns:
//
//	["subdomain.example.com", "cdn.example.com", "cdn.cloudflare.com"]
func (r *RecordsCache) GetTargetChain(domain string) []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	now := time.Now()
	result := []string{domain}
	visited := make(map[string]bool)
	visited[domain] = true
	current := domain

	for {
		entry, exists := r.aliases[current]
		if !exists || entry.Deadline.Before(now) {
			break
		}
		if visited[entry.Target] {
			// Avoid infinite loop in case of circular CNAMEs
			break
		}
		visited[entry.Target] = true
		result = append(result, entry.Target)
		current = entry.Target
	}

	return result
}

// Cleanup removes expired entries from the cache.
func (r *RecordsCache) Cleanup() {
	r.mu.RLock()
	now := time.Now()

	// Scan for expired addresses
	var expiredDomains []string
	for domain, addrs := range r.addresses {
		for _, addr := range addrs {
			if !addr.Deadline.After(now) {
				expiredDomains = append(expiredDomains, domain)
				break
			}
		}
	}

	// Scan for expired aliases
	var expiredAliases []string
	for domain, entry := range r.aliases {
		if entry.Deadline.Before(now) {
			expiredAliases = append(expiredAliases, domain)
		}
	}
	r.mu.RUnlock()

	// If nothing to clean, return immediately
	if len(expiredDomains) == 0 && len(expiredAliases) == 0 {
		return
	}

	// Acquire write lock to perform cleanup
	r.mu.Lock()
	defer r.mu.Unlock()

	// Re-check time as it might have advanced slightly
	now = time.Now()

	// Cleanup addresses
	for _, domain := range expiredDomains {
		if addrs, exists := r.addresses[domain]; exists {
			var valid []CachedAddress
			for _, addr := range addrs {
				if addr.Deadline.After(now) {
					valid = append(valid, addr)
				}
			}
			if len(valid) == 0 {
				delete(r.addresses, domain)
			} else {
				r.addresses[domain] = valid
			}
		}
	}

	// Cleanup aliases
	for _, domain := range expiredAliases {
		if entry, exists := r.aliases[domain]; exists {
			if entry.Deadline.Before(now) {
				delete(r.aliases, domain)
			}
		}
	}
}

// Clear removes all entries from the cache.
func (r *RecordsCache) Clear() {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.addresses = make(map[string][]CachedAddress)
	r.aliases = make(map[string]aliasEntry)
}

// Stats returns cache statistics.
func (r *RecordsCache) Stats() (addressCount, aliasCount int) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	for _, addrs := range r.addresses {
		addressCount += len(addrs)
	}
	aliasCount = len(r.aliases)

	return
}
