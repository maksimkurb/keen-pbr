# DNS Records Cache Optimization Plan

## Overview
Refactor `src/internal/dnsproxy/caching/records_cache.go` to improve performance, memory efficiency, and code simplicity by adopting proven patterns from dnsmasq's cache implementation.

## Critical Files
- `src/internal/dnsproxy/caching/records_cache.go` (main implementation)
- `src/internal/dnsproxy/caching/records_cache_test.go` (tests)
- `src/internal/dnsproxy/proxy.go` (cache usage)

## Problems Identified

### 1. Wrong LRU Granularity
**Current:** LRU tracks domains, not individual addresses
- Domain with 100 IPs has same weight as domain with 1 IP
- Unfair eviction (small domains evicted while large ones stay)
- Memory usage unpredictable

### 2. Complex CNAME Handling
**Current:** String-based aliases with reverse map
- `reverseAliases` map needs rebuilding
- `reverseValid` flag state management
- BFS traversal to find aliases
- Extra memory overhead

### 3. Inefficient Expiration
**Current:** Requires explicit `EvictExpiredEntries()` calls
- Full map iteration every time
- Needs periodic goroutine
- Cleans entries that may never be accessed

### 4. Lock Contention
**Current:** Single `sync.RWMutex` for entire cache
- Write locks block all operations
- Read locks prevent writes
- Bottleneck under high concurrency

### 5. Race Conditions & Bugs
- `GetAliases` takes write lock but mostly reads
- LRU inconsistency: domains with both address and alias
- `touchDomain` called even when returning false
- Eviction boundary off-by-one (`>` should be `>=`)

## Optimization Strategy

### Phase 1: Fix Critical Bugs (Low Risk)
Simple fixes that don't require redesign.

**Changes:**
1. Fix eviction boundary: `Len() > maxDomains` → `Len() >= maxDomains`
2. Only call `touchDomain` when actually modifying cache
3. Remove LRU entry when domain has no addresses AND no aliases
4. Fix `EvictExpiredEntries` to properly clean both maps and LRU

**Files:** `records_cache.go:107, 153, 304-334`

### Phase 2: Implement Lock Sharding (Medium Risk)
Reduce lock contention with minimal design changes.

**Design:**
```go
type shard struct {
    mu             sync.RWMutex
    addresses      map[string][]cachedAddressInternal
    aliases        map[string]aliasEntryInternal
    reverseAliases map[string][]string
    reverseValid   bool
    lruList        *list.List
    lruIndex       map[string]*list.Element
}

type RecordsCache struct {
    shards     []shard
    numShards  int
    maxPerShard int
}
```

**Hash function:**
```go
func (r *RecordsCache) getShard(domain string) *shard {
    h := fnv.New32a()
    h.Write([]byte(domain))
    return &r.shards[h.Sum32()%uint32(r.numShards)]
}
```

**Benefits:**
- Concurrent access to different shards
- Reduced lock contention
- Standard pattern in Go

**Changes:**
- Split cache into N shards (start with 16)
- Each shard has independent lock and LRU
- Route operations by domain hash
- `Stats()` aggregates across shards

**Files:** `records_cache.go:57-91, all methods`

### Phase 3: Implement Lazy Expiration (Medium Risk)
Remove need for periodic cleanup, improve cache locality.

**Changes:**
1. Remove `EvictExpiredEntries()` method entirely
2. Add expiration check in `GetAddresses()`:
   ```go
   func (s *shard) getAddresses(domain string, now int64) []CachedAddress {
       addrs := s.addresses[domain]
       valid := addrs[:0] // reuse backing array
       for _, addr := range addrs {
           if addr.deadline > now {
               valid = append(valid, addr)
           }
       }
       if len(valid) == 0 {
           delete(s.addresses, domain)
           s.removeDomainIfEmpty(domain)
       } else if len(valid) < len(addrs) {
           s.addresses[domain] = valid
       }
       return valid
   }
   ```
3. Add expiration check in `GetAliases()` during traversal
4. Add expiration check in `evictIfNeeded()` before LRU eviction

**Benefits:**
- No background goroutine needed
- Only clean accessed entries
- Better cache locality
- Simpler API

**Files:** `records_cache.go:212-236, 240-271, 304-334`

### Phase 4: Simplified Reverse Map + Optional UID Staleness (Medium Impact)
Simplify reverse alias management without sacrificing performance.

**REVISED APPROACH** (after usage analysis):
- Keep reverse map for efficient O(1) reverse lookups (required by proxy.go)
- Maintain reverse map incrementally (no rebuild, no `reverseValid` flag)
- Add optional UID-based staleness for forward chain validation
- Simplify LRU by using metadata map instead of list+index

**Design:**
```go
type domainMetadata struct {
    uid      uint32 // incremented on modification (optional optimization)
    lastUsed int64  // for LRU tracking
}

type aliasEntryInternal struct {
    target      string
    deadline    int64
}

type shard struct {
    mu        sync.RWMutex
    addresses map[string][]cachedAddressInternal
    aliases   map[string]aliasEntryInternal

    // Incrementally maintained reverse map (no rebuild needed)
    reverseAliases map[string][]string

    // NEW: unified metadata for LRU and optional UID tracking
    metadata  map[string]domainMetadata

    // REMOVED: reverseValid flag, lruList, lruIndex
}
```

**Incremental Reverse Map Updates:**
```go
func (s *shard) addAlias(domain, target string, ttl uint32, now int64) {
    deadline := now + int64(ttl)

    // Check if alias changed (need to update reverse map)
    if existing, exists := s.aliases[domain]; exists && existing.target != target {
        // Remove old reverse mapping
        s.removeFromReverseAliases(existing.target, domain)
    }

    // Add new alias
    s.aliases[domain] = aliasEntryInternal{
        target:   target,
        deadline: deadline,
    }

    // Add to reverse map incrementally
    s.reverseAliases[target] = append(s.reverseAliases[target], domain)

    // Update metadata
    meta := s.metadata[domain]
    meta.lastUsed = now
    s.metadata[domain] = meta
}

func (s *shard) removeFromReverseAliases(target, source string) {
    sources := s.reverseAliases[target]
    for i, src := range sources {
        if src == source {
            // Remove by replacing with last element
            sources[i] = sources[len(sources)-1]
            s.reverseAliases[target] = sources[:len(sources)-1]
            break
        }
    }
    if len(s.reverseAliases[target]) == 0 {
        delete(s.reverseAliases, target)
    }
}
```

**GetAliases with Lazy Expiration:**
```go
func (s *shard) getAliases(domain string, now int64) []string {
    result := make([]string, 1, 8)
    result[0] = domain

    visited := make(map[string]struct{}, 8)
    visited[domain] = struct{}{}

    // BFS - use result slice as queue
    for i := 0; i < len(result); i++ {
        current := result[i]
        sources := s.reverseAliases[current]

        // Filter expired sources lazily
        validSources := sources[:0]
        for _, source := range sources {
            entry, exists := s.aliases[source]
            if !exists {
                continue // alias was deleted
            }
            if entry.deadline <= now {
                // Expired - will be removed
                continue
            }
            if entry.target != current {
                // Stale entry (target changed) - will be removed
                continue
            }

            // Valid alias
            validSources = append(validSources, source)
            if _, seen := visited[source]; !seen {
                visited[source] = struct{}{}
                result = append(result, source)
            }
        }

        // Update reverse map if we filtered anything
        if len(validSources) < len(sources) {
            if len(validSources) == 0 {
                delete(s.reverseAliases, current)
            } else {
                s.reverseAliases[current] = validSources
            }
        }
    }

    return result
}
```

**Simplified LRU with Metadata Map:**
```go
func (s *shard) evictIfNeeded(now int64) {
    if len(s.metadata) <= s.maxPerShard {
        return
    }

    // Find oldest domain by scanning metadata
    // This is O(n) but eviction is rare and n is small per shard
    var oldestDomain string
    oldestTime := now

    for domain, meta := range s.metadata {
        if meta.lastUsed < oldestTime {
            oldestTime = meta.lastUsed
            oldestDomain = domain
        }
    }

    if oldestDomain != "" {
        s.removeDomain(oldestDomain)
    }
}

func (s *shard) removeDomain(domain string) {
    // Remove addresses
    delete(s.addresses, domain)

    // Remove alias and reverse mapping
    if entry, exists := s.aliases[domain]; exists {
        s.removeFromReverseAliases(entry.target, domain)
        delete(s.aliases, domain)
    }

    // Remove from metadata
    delete(s.metadata, domain)
}
```

**Benefits:**
- Keeps O(1) reverse lookup (critical for proxy.go usage)
- Eliminates `reverseValid` flag and rebuild logic
- Reverse map maintained incrementally (always correct)
- Lazy expiration cleans reverse map during lookups
- Simpler LRU (no list+index, just metadata map)
- No write lock escalation in GetAliases

**Trade-offs:**
- LRU eviction is O(n) scan per shard (acceptable, eviction is rare)
- Slightly more memory (metadata map) but simpler code
- Could optimize LRU with min-heap later if needed

**Optional Future Enhancement:**
- Add UID to metadata for staleness detection in GetTargetChain
- Useful if forward chain validation becomes a bottleneck
- Not critical for current usage patterns

**Files:** `records_cache.go:48-76, 93-130, 174-209, 240-271`

### Phase 5: Redesign GetAliases API (Critical Insight)
Address confusion around GetAliases semantics and actual usage.

**Current behavior:** Returns all domains that point TO the given domain (reverse lookup)
**Problem:** Naming is misleading, requires reverse map

**Actual Usage Analysis (from proxy.go):**

1. **collectIPSetEntries (line 1028):**
   - Called with: `GetAliases(domain)` where domain is the one being resolved
   - Purpose: For a domain like `cdn.example.com`, find all domains that point to it (e.g., `www.example.com -> cdn.example.com`)
   - Then checks each alias against matcher to see if it should be in ipsets

2. **processCNAMERecord (line 1104):**
   - Called with: `GetAliases(domain)` where domain is the CNAME source
   - Purpose: After adding `domain -> target`, find all domains that point to `domain`
   - Example: If `a.com -> b.com` and we add `b.com -> c.com`, find that `a.com` should also resolve

**Why Reverse Lookup is ESSENTIAL:**

Consider this scenario:
1. User has rule: "Add `www.example.com` to ipset"
2. DNS query for `www.example.com` returns CNAME: `www.example.com -> cdn.cloudflare.net`
3. Then we get A record: `cdn.cloudflare.net -> 1.2.3.4`
4. When processing the A record, we need to know that `www.example.com` points to `cdn.cloudflare.net`
5. So we check if `www.example.com` matches the matcher and add the IP to ipset

**GetAliases is doing exactly this:** given `cdn.cloudflare.net`, return `[cdn.cloudflare.net, www.example.com]`

**Options:**

**Option A: Keep GetAliases but simplify with UID approach**
- Reverse lookup IS needed for the ipset matching logic
- UID-based staleness eliminates the reverse map
- But we need a different way to do reverse lookup with UIDs
- **Problem:** UID approach makes reverse lookup harder (need to scan all aliases)

**Option B: Rename to clarify semantics**
```go
// Forward chain (CNAME target lookup)
func GetTargetChain(domain string) []string

// Reverse lookup (what points to this domain)
func GetSourceDomains(domain string) []string  // rename from GetAliases
```

**Option C: Keep GetAliases name, keep reverse map, but simplify its management**
- Incrementally update reverse map in AddAlias (no rebuild needed)
- Remove `reverseValid` flag
- Still use lazy expiration during lookups

**CRITICAL REALIZATION:** UID-based approach conflicts with efficient reverse lookup!
- UID approach eliminates reverse map (good for memory)
- But reverse lookup requires scanning all aliases (O(n) every call)
- Current cached reverse map is O(1) after initial build

**Revised Recommendation for Phase 4:**
- **DO NOT** eliminate reverse map entirely
- **INSTEAD:** Maintain reverse map incrementally in AddAlias/RemoveAlias
- Keep UID for staleness detection within forward lookups (GetTargetChain)
- Use lazy expiration to clean reverse map during GetAliases calls

**Files:** `records_cache.go:238-271`, `proxy.go:1028,1104`

## Implementation Order

1. **Phase 1** - Bug fixes (1-2 hours)
   - Low risk, immediate improvements
   - Add tests for edge cases

2. **Phase 2** - Lock sharding (3-4 hours)
   - Significant concurrency improvement
   - Benchmark before/after

3. **Phase 3** - Lazy expiration (2-3 hours)
   - Simplifies API and usage
   - Remove periodic cleanup goroutines

4. **Phase 5** - GetAliases API review (1 hour)
   - Check actual usage first
   - Decide keep/remove/rename

5. **Phase 4** - UID-based staleness (4-6 hours)
   - Most complex change
   - Do last when everything else stable
   - Comprehensive testing needed

## Testing Strategy

1. **Unit tests** for each phase:
   - Concurrent access (race detector)
   - Eviction behavior
   - CNAME chain handling
   - Expiration edge cases

2. **Benchmarks:**
   - Cache hit/miss performance
   - Concurrent access scaling
   - Memory usage profiling

3. **Integration tests:**
   - Full DNS query flow with cache
   - CNAME chain resolution
   - TTL expiration scenarios

## Success Metrics

- ✅ Reduced lock contention (sharding)
- ✅ Lower memory usage (no reverse map, simpler LRU)
- ✅ Simpler code (lazy expiration, UID staleness)
- ✅ No periodic cleanup needed
- ✅ Fair eviction (proper granularity)
- ✅ All tests pass with race detector
