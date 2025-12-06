package caching

import (
	"container/list"
	"net"
	"sync"
	"time"
)

// extractIPv4Count extracts IPv4 count from the combined byte (lower 5 bits).
func extractIPv4Count(b byte) int {
	return int(b & 0x1F) // Lower 5 bits
}

// extractIPv6Count extracts IPv6 count from the combined byte (upper 3 bits).
func extractIPv6Count(b byte) int {
	return int(b >> 5) // Upper 3 bits
}

// packCounts packs IPv4 and IPv6 counts into a single byte.
func packCounts(ipv4Count, ipv6Count int) byte {
	return byte(ipv4Count&0x1F) | byte((ipv6Count&0x07)<<5)
}

// CachedAddress represents a cached IP address with its expiration time.
type CachedAddress struct {
	Address  net.IP
	Deadline time.Time
}

// aliasEntryInternal is the memory-optimized internal alias entry.
type aliasEntryInternal struct {
	target   string
	deadline int64 // Unix timestamp in seconds
}

// addressEntry stores IP addresses in a compact variable-size representation.
// counts: lower 5 bits = IPv4 count (0-31), upper 3 bits = IPv6 count (0-7)
// addrs: byte slice containing [ipv4_addresses...][ipv6_addresses...]
//   - ipv4_addresses: each IPv4 is 4 bytes
//   - ipv6_addresses: each IPv6 is 16 bytes
//
// Size: exactly ipv4Count*4 + ipv6Count*16 bytes (no overhead)
type addressEntry struct {
	counts      byte   // Packed IPv4/IPv6 counts
	addrs       []byte // Dynamic byte array: only allocated size needed
	minDeadline int64  // Earliest expiration across all addresses
}

// getCounts returns the number of IPv4 and IPv6 addresses.
func (e *addressEntry) getCounts() (ipv4Count, ipv6Count int) {
	ipv4Count = extractIPv4Count(e.counts)
	ipv6Count = extractIPv6Count(e.counts)
	return
}

// hasAddress checks if an IP address already exists in the entry.
func (e *addressEntry) hasAddress(ip net.IP) bool {
	ipv4Count, ipv6Count := e.getCounts()

	if ip4 := ip.To4(); ip4 != nil {
		// Check IPv4 addresses
		offset := 0
		for i := 0; i < ipv4Count; i++ {
			if ip4[0] == e.addrs[offset] &&
				ip4[1] == e.addrs[offset+1] &&
				ip4[2] == e.addrs[offset+2] &&
				ip4[3] == e.addrs[offset+3] {
				return true
			}
			offset += 4
		}
	} else if ip16 := ip.To16(); ip16 != nil {
		// Check IPv6 addresses
		offset := ipv4Count * 4
		for i := 0; i < ipv6Count; i++ {
			match := true
			for j := 0; j < 16; j++ {
				if ip16[j] != e.addrs[offset+j] {
					match = false
					break
				}
			}
			if match {
				return true
			}
			offset += 16
		}
	}
	return false
}

// addAddress adds an IP address to the entry. Returns false if full or duplicate.
func (e *addressEntry) addAddress(ip net.IP) bool {
	ipv4Count, ipv6Count := e.getCounts()

	if ip4 := ip.To4(); ip4 != nil {
		// Add IPv4
		if ipv4Count >= 31 {
			return false // Full
		}
		if e.hasAddress(ip) {
			return false // Duplicate
		}

		// Expand slice to fit new IPv4 (insert at IPv4 section end, before IPv6s)
		newSize := len(e.addrs) + 4
		newAddrs := make([]byte, newSize)

		// Copy existing IPv4s
		ipv4End := ipv4Count * 4
		copy(newAddrs[0:ipv4End], e.addrs[0:ipv4End])

		// Insert new IPv4
		copy(newAddrs[ipv4End:ipv4End+4], ip4)

		// Copy existing IPv6s
		if ipv6Count > 0 {
			copy(newAddrs[ipv4End+4:], e.addrs[ipv4End:])
		}

		e.addrs = newAddrs
		e.counts = packCounts(ipv4Count+1, ipv6Count)
		return true
	} else if ip16 := ip.To16(); ip16 != nil {
		// Add IPv6
		if ipv6Count >= 7 {
			return false // Full
		}
		if e.hasAddress(ip) {
			return false // Duplicate
		}

		// Expand slice to fit new IPv6 (append at end)
		e.addrs = append(e.addrs, ip16...)
		e.counts = packCounts(ipv4Count, ipv6Count+1)
		return true
	}
	return false
}

// getAllAddresses returns all addresses as []net.IP.
func (e *addressEntry) getAllAddresses(deadline time.Time) []CachedAddress {
	ipv4Count, ipv6Count := e.getCounts()
	total := ipv4Count + ipv6Count
	result := make([]CachedAddress, 0, total)

	// Extract IPv4 addresses
	offset := 0
	for i := 0; i < ipv4Count; i++ {
		ip := net.IP(make([]byte, 4))
		copy(ip, e.addrs[offset:offset+4])
		result = append(result, CachedAddress{
			Address:  ip,
			Deadline: deadline,
		})
		offset += 4
	}

	// Extract IPv6 addresses
	for i := 0; i < ipv6Count; i++ {
		ip := net.IP(make([]byte, 16))
		copy(ip, e.addrs[offset:offset+16])
		result = append(result, CachedAddress{
			Address:  ip,
			Deadline: deadline,
		})
		offset += 16
	}

	return result
}

// clear resets the entry to empty.
func (e *addressEntry) clear() {
	e.counts = 0
	e.addrs = nil
	e.minDeadline = 0
}

// RecordsCache stores DNS records (addresses and CNAME aliases) with TTL-based expiration.
// It tracks the relationship between domain names and their resolved addresses,
// including CNAME chain resolution.
type RecordsCache struct {
	mu sync.RWMutex

	// addresses maps domain name -> address entry with minimum deadline tracking
	addresses map[string]addressEntry

	// aliases maps domain name -> target domain name (CNAME)
	aliases map[string]aliasEntryInternal

	// reverseAliases maps target -> list of sources (cached for fast reverse lookups)
	reverseAliases map[string][]string
	reverseValid   bool // true if reverseAliases is up-to-date

	// maxDomains limits the number of domains in the cache
	maxDomains int

	// LRU tracking using doubly-linked list for O(1) operations
	lruList  *list.List               // list of domain strings (front = oldest)
	lruIndex map[string]*list.Element // domain -> list element
}

// NewRecordsCache creates a new records cache with the specified maximum number of domains.
func NewRecordsCache(maxDomains int) *RecordsCache {
	if maxDomains <= 0 {
		panic("maxDomains must be positive")
	}
	return &RecordsCache{
		addresses:      make(map[string]addressEntry),
		aliases:        make(map[string]aliasEntryInternal),
		reverseAliases: make(map[string][]string),
		maxDomains:     maxDomains,
		lruList:        list.New(),
		lruIndex:       make(map[string]*list.Element),
	}
}

// touchDomain moves a domain to the back of LRU list (most recently used).
// Must be called with write lock held.
func (r *RecordsCache) touchDomain(domain string) {
	if elem, exists := r.lruIndex[domain]; exists {
		r.lruList.MoveToBack(elem)
	} else {
		elem := r.lruList.PushBack(domain)
		r.lruIndex[domain] = elem
	}
}

// evictIfNeeded evicts the least recently used domains if cache is full.
// Must be called with write lock held.
func (r *RecordsCache) evictIfNeeded() {
	for r.lruList.Len() > r.maxDomains {
		elem := r.lruList.Front()
		if elem == nil {
			break
		}
		oldest := elem.Value.(string)
		r.lruList.Remove(elem)
		delete(r.lruIndex, oldest)
		delete(r.addresses, oldest)
		if _, hasAlias := r.aliases[oldest]; hasAlias {
			delete(r.aliases, oldest)
			r.reverseValid = false
		}
	}
}

// removeDomainFromLRU removes a domain from LRU tracking.
// Must be called with write lock held.
func (r *RecordsCache) removeDomainFromLRU(domain string) {
	if elem, exists := r.lruIndex[domain]; exists {
		r.lruList.Remove(elem)
		delete(r.lruIndex, domain)
	}
}

// AddAddress adds an IP address for a domain with the specified TTL.
// Returns true if this is a new entry that should be added to ipset,
// false if the entry already exists and is still valid.
func (r *RecordsCache) AddAddress(domain string, address net.IP, ttl uint32) bool {
	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now().Unix()
	deadline := now + int64(ttl)

	entry, exists := r.addresses[domain]

	// Check if entry exists and is still valid
	if exists {
		// Check if address already in entry
		if entry.hasAddress(address) {
			// Entry exists - check if it's still valid
			if entry.minDeadline > now {
				// Entry is still valid, update deadline if new one is later
				if deadline > entry.minDeadline {
					entry.minDeadline = deadline
					r.addresses[domain] = entry
				}
				r.touchDomain(domain)
				return false // Already in cache and valid
			}
			// Entry expired, clear and re-add
			entry.clear()
			entry.minDeadline = deadline
			entry.addAddress(address)
			r.addresses[domain] = entry
			r.touchDomain(domain)
			return true
		}

		// Address not in entry - add it
		if entry.addAddress(address) {
			// Update minDeadline if this is earlier
			if deadline < entry.minDeadline {
				entry.minDeadline = deadline
			}
			r.addresses[domain] = entry
			r.touchDomain(domain)
			r.evictIfNeeded()
			return true
		}
		// Entry is full, can't add
		return false
	}

	// Create new entry
	entry.clear()
	entry.minDeadline = deadline
	if !entry.addAddress(address) {
		return false // Should never happen for new entry
	}

	r.addresses[domain] = entry
	r.touchDomain(domain)
	r.evictIfNeeded()
	return true
}

// AddAlias adds a CNAME alias (domain -> target) with the specified TTL.
// Self-referential aliases (domain == target) are ignored.
func (r *RecordsCache) AddAlias(domain, target string, ttl uint32) {
	if domain == target {
		return // Ignore self-referential aliases
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	deadline := time.Now().Unix() + int64(ttl)

	// Check if alias changed
	if existing, exists := r.aliases[domain]; !exists || existing.target != target {
		r.reverseValid = false
	}

	r.aliases[domain] = aliasEntryInternal{
		target:   target,
		deadline: deadline,
	}
	r.touchDomain(domain)
	r.evictIfNeeded()
}

// rebuildReverseAliases rebuilds the reverse alias map.
// Must be called with write lock held.
func (r *RecordsCache) rebuildReverseAliases(now int64) {
	r.reverseAliases = make(map[string][]string, len(r.aliases))
	for source, entry := range r.aliases {
		if entry.deadline > now {
			r.reverseAliases[entry.target] = append(r.reverseAliases[entry.target], source)
		}
	}
	r.reverseValid = true
}

// GetAddresses returns all non-expired addresses for a domain.
// If ANY address has expired, the entire domain entry is invalidated and nil is returned.
func (r *RecordsCache) GetAddresses(domain string) []CachedAddress {
	r.mu.RLock()

	entry, exists := r.addresses[domain]
	if !exists {
		r.mu.RUnlock()
		return nil
	}

	now := time.Now().Unix()

	// Fast path: All addresses guaranteed valid if minDeadline not reached
	if now < entry.minDeadline {
		result := entry.getAllAddresses(time.Unix(entry.minDeadline, 0))
		r.mu.RUnlock()
		return result
	}

	// Slow path: Entry expired - upgrade to write lock
	r.mu.RUnlock()
	r.mu.Lock()
	defer r.mu.Unlock()

	// Re-check after lock upgrade (race condition protection)
	entry, exists = r.addresses[domain]
	if !exists {
		return nil
	}

	now = time.Now().Unix() // Re-read time after lock upgrade

	// Check if expired
	if entry.minDeadline <= now {
		// Invalidate entire domain entry atomically
		delete(r.addresses, domain)
		r.removeDomainFromLRU(domain)

		// Also invalidate any alias pointing FROM this domain
		if _, exists := r.aliases[domain]; exists {
			delete(r.aliases, domain)
			r.reverseValid = false
		}

		return nil // Cache miss - will trigger upstream query
	}

	// Still valid after re-check, build result
	result := entry.getAllAddresses(time.Unix(entry.minDeadline, 0))
	return result
}

// GetAliases returns all domain names that resolve to the given domain,
// including the domain itself and all CNAMEs in the chain.
func (r *RecordsCache) GetAliases(domain string) []string {
	r.mu.Lock() // Need write lock to potentially rebuild reverse map
	defer r.mu.Unlock()

	now := time.Now().Unix()

	// Rebuild reverse map if needed
	if !r.reverseValid {
		r.rebuildReverseAliases(now)
	}

	result := make([]string, 1, 8)
	result[0] = domain

	visited := make(map[string]struct{}, 8)
	visited[domain] = struct{}{}

	// BFS - use result slice as queue
	for i := 0; i < len(result); i++ {
		current := result[i]
		if sources, exists := r.reverseAliases[current]; exists {
			for _, source := range sources {
				if _, seen := visited[source]; !seen {
					visited[source] = struct{}{}
					result = append(result, source)
				}
			}
		}
	}

	return result
}

// GetTargetChain returns the CNAME chain for a domain.
func (r *RecordsCache) GetTargetChain(domain string) []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	now := time.Now().Unix()

	result := make([]string, 1, 4)
	result[0] = domain

	visited := make(map[string]struct{}, 4)
	visited[domain] = struct{}{}
	current := domain

	for {
		entry, exists := r.aliases[current]
		if !exists || entry.deadline <= now {
			break
		}
		if _, seen := visited[entry.target]; seen {
			break // Circular CNAME
		}
		visited[entry.target] = struct{}{}
		result = append(result, entry.target)
		current = entry.target
	}

	return result
}

// EvictExpiredEntries removes expired entries from the cache.
// When ANY address in a domain entry has expired, the entire domain is removed.
func (r *RecordsCache) EvictExpiredEntries() {
	// Use write lock from start - simpler and avoids race with lock upgrade
	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now().Unix()

	// Clean addresses - atomic deletion when ANY IP expired
	for domain, entry := range r.addresses {
		// Check if minDeadline expired
		if entry.minDeadline <= now {
			// Atomic deletion: Remove entire domain entry
			delete(r.addresses, domain)
			r.removeDomainFromLRU(domain)
		}
	}

	// Clean aliases
	for domain, entry := range r.aliases {
		if entry.deadline <= now {
			delete(r.aliases, domain)
			r.reverseValid = false
		}
	}
}

// Clear removes all entries from the cache.
func (r *RecordsCache) Clear() {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.addresses = make(map[string]addressEntry)
	r.aliases = make(map[string]aliasEntryInternal)
	r.reverseAliases = make(map[string][]string)
	r.reverseValid = false
	r.lruList = list.New()
	r.lruIndex = make(map[string]*list.Element)
}

// Stats returns cache statistics.
func (r *RecordsCache) Stats() (addressCount, aliasCount int) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	for _, entry := range r.addresses {
		ipv4Count, ipv6Count := entry.getCounts()
		addressCount += ipv4Count + ipv6Count
	}
	aliasCount = len(r.aliases)

	return
}
