package dnsproxy

import (
	"container/list"
	"net"
	"sync"
	"time"
)

// IP16 is a fixed-size representation of an IP address (both IPv4 and IPv6).
type IP16 struct {
	addr   [16]byte
	isIPv4 bool
}

// ToIP converts IP16 back to net.IP.
func (ip IP16) ToIP() net.IP {
	if ip.isIPv4 {
		return net.IP(ip.addr[:4])
	}
	return net.IP(ip.addr[:])
}

// IP16FromNetIP converts net.IP to IP16.
func IP16FromNetIP(ip net.IP) IP16 {
	var result IP16
	if ip4 := ip.To4(); ip4 != nil {
		result.isIPv4 = true
		copy(result.addr[:4], ip4)
	} else if ip16 := ip.To16(); ip16 != nil {
		copy(result.addr[:], ip16)
	}
	return result
}

// CachedAddress represents a cached IP address with its expiration time.
type CachedAddress struct {
	Address  net.IP
	Deadline time.Time
}

// cachedAddressInternal is the memory-optimized internal representation.
type cachedAddressInternal struct {
	address  IP16
	deadline int64 // Unix timestamp in seconds
}

// aliasEntryInternal is the memory-optimized internal alias entry.
type aliasEntryInternal struct {
	target   string
	deadline int64 // Unix timestamp in seconds
}

// RecordsCache stores DNS records (addresses and CNAME aliases) with TTL-based expiration.
// It tracks the relationship between domain names and their resolved addresses,
// including CNAME chain resolution.
type RecordsCache struct {
	mu sync.RWMutex

	// addresses maps domain name -> list of cached addresses
	addresses map[string][]cachedAddressInternal

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
		addresses:      make(map[string][]cachedAddressInternal),
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
	addr16 := IP16FromNetIP(address)

	// Check if address already exists
	if addrs, exists := r.addresses[domain]; exists {
		for i, addr := range addrs {
			if addr.address == addr16 {
				// Entry exists - check if it's still valid
				if addr.deadline > now {
					// Entry is still valid, just update deadline if new one is later
					if deadline > addr.deadline {
						r.addresses[domain][i].deadline = deadline
					}
					r.touchDomain(domain)
					return false // Already in cache and valid
				}
				// Entry expired, update it
				r.addresses[domain][i].deadline = deadline
				r.touchDomain(domain)
				return true
			}
		}
	}

	// Add new address
	r.addresses[domain] = append(r.addresses[domain], cachedAddressInternal{
		address:  addr16,
		deadline: deadline,
	})
	r.touchDomain(domain)
	r.evictIfNeeded()
	return true
}

// AddAlias adds a CNAME alias (domain -> target) with the specified TTL.
func (r *RecordsCache) AddAlias(domain, target string, ttl uint32) {
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
func (r *RecordsCache) GetAddresses(domain string) []CachedAddress {
	r.mu.RLock()
	defer r.mu.RUnlock()

	now := time.Now().Unix()
	addrs, exists := r.addresses[domain]
	if !exists {
		return nil
	}

	var result []CachedAddress
	for _, addr := range addrs {
		if addr.deadline > now {
			if result == nil {
				result = make([]CachedAddress, 0, len(addrs))
			}
			result = append(result, CachedAddress{
				Address:  addr.address.ToIP(),
				Deadline: time.Unix(addr.deadline, 0),
			})
		}
	}

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

// Cleanup removes expired entries from the cache.
func (r *RecordsCache) Cleanup() {
	r.mu.RLock()
	now := time.Now().Unix()

	var expiredDomains []string
	var expiredAliases []string

	for domain, addrs := range r.addresses {
		hasValid := false
		for _, addr := range addrs {
			if addr.deadline > now {
				hasValid = true
				break
			}
		}
		if !hasValid {
			expiredDomains = append(expiredDomains, domain)
		}
	}

	for domain, entry := range r.aliases {
		if entry.deadline <= now {
			expiredAliases = append(expiredAliases, domain)
		}
	}
	r.mu.RUnlock()

	if len(expiredDomains) == 0 && len(expiredAliases) == 0 {
		return
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	now = time.Now().Unix()

	for _, domain := range expiredDomains {
		if addrs, exists := r.addresses[domain]; exists {
			var valid []cachedAddressInternal
			for _, addr := range addrs {
				if addr.deadline > now {
					valid = append(valid, addr)
				}
			}
			if len(valid) == 0 {
				delete(r.addresses, domain)
				r.removeDomainFromLRU(domain)
			} else {
				r.addresses[domain] = valid
			}
		}
	}

	for _, domain := range expiredAliases {
		if entry, exists := r.aliases[domain]; exists {
			if entry.deadline <= now {
				delete(r.aliases, domain)
				r.reverseValid = false
			}
		}
	}
}

// Clear removes all entries from the cache.
func (r *RecordsCache) Clear() {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.addresses = make(map[string][]cachedAddressInternal)
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

	for _, addrs := range r.addresses {
		addressCount += len(addrs)
	}
	aliasCount = len(r.aliases)

	return
}
