package caching

import (
	"fmt"
	"math/rand"
	"net"
	"sync"
	"testing"
	"time"
)

func TestRecordsCache_AddAddress(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add an address
	ip := net.ParseIP("1.2.3.4")
	cache.AddAddress("example.com", ip, 300)

	// Get addresses
	addrs := cache.GetAddresses("example.com")
	if len(addrs) != 1 {
		t.Errorf("expected 1 address, got %d", len(addrs))
	}

	if !addrs[0].Address.Equal(ip) {
		t.Errorf("expected %s, got %s", ip, addrs[0].Address)
	}
}

func TestRecordsCache_AddAlias(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add CNAME chain: sub.example.com -> cdn.example.com -> cloudflare.com
	cache.AddAlias("sub.example.com", "cdn.example.com", 300)
	cache.AddAlias("cdn.example.com", "cloudflare.com", 300)

	// GetAliases should return all domains that resolve to cloudflare.com
	aliases := cache.GetAliases("cloudflare.com")

	expected := map[string]bool{
		"cloudflare.com":  true,
		"cdn.example.com": true,
		"sub.example.com": true,
	}

	if len(aliases) != len(expected) {
		t.Errorf("expected %d aliases, got %d: %v", len(expected), len(aliases), aliases)
	}

	for _, alias := range aliases {
		if !expected[alias] {
			t.Errorf("unexpected alias: %s", alias)
		}
	}
}

func TestRecordsCache_GetTargetChain(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add CNAME chain
	cache.AddAlias("sub.example.com", "cdn.example.com", 300)
	cache.AddAlias("cdn.example.com", "cloudflare.com", 300)

	// Get target chain starting from sub.example.com
	chain := cache.GetTargetChain("sub.example.com")

	expected := []string{"sub.example.com", "cdn.example.com", "cloudflare.com"}

	if len(chain) != len(expected) {
		t.Errorf("expected chain length %d, got %d: %v", len(expected), len(chain), chain)
	}

	for i, domain := range expected {
		if chain[i] != domain {
			t.Errorf("chain[%d]: expected %s, got %s", i, domain, chain[i])
		}
	}
}

func TestRecordsCache_Cleanup(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add an address with very short TTL
	ip := net.ParseIP("1.2.3.4")
	cache.AddAddress("example.com", ip, 0) // TTL 0 = expires immediately

	// Wait a moment
	time.Sleep(10 * time.Millisecond)

	// Run cleanup
	cache.EvictExpiredEntries()

	// Address should be gone
	addrs := cache.GetAddresses("example.com")
	if len(addrs) != 0 {
		t.Errorf("expected 0 addresses after cleanup, got %d", len(addrs))
	}
}

func TestRecordsCache_Clear(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add some data
	cache.AddAddress("example.com", net.ParseIP("1.2.3.4"), 300)
	cache.AddAlias("www.example.com", "example.com", 300)

	// Clear
	cache.Clear()

	// Check stats
	addrCount, aliasCount := cache.Stats()
	if addrCount != 0 || aliasCount != 0 {
		t.Errorf("expected empty cache, got %d addresses, %d aliases", addrCount, aliasCount)
	}
}

func TestRecordsCache_ConcurrencyLifecycle(t *testing.T) {
	cache := NewRecordsCache(10000)
	var wg sync.WaitGroup
	stop := make(chan struct{})

	// Configuration
	const (
		numWriters = 10
		numReaders = 20
		duration   = 1 * time.Second
		ttl        = 1 // 1 second TTL
	)

	// Writers: Add addresses and aliases
	for i := 0; i < numWriters; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for {
				select {
				case <-stop:
					return
				default:
					domain := fmt.Sprintf("domain-%d.com", rand.Intn(100))
					ip := net.ParseIP(fmt.Sprintf("1.2.3.%d", rand.Intn(255)))
					cache.AddAddress(domain, ip, ttl)

					alias := fmt.Sprintf("alias-%d.com", rand.Intn(100))
					target := fmt.Sprintf("target-%d.com", rand.Intn(100))
					cache.AddAlias(alias, target, ttl)

					time.Sleep(time.Millisecond * 10)
				}
			}
		}(i)
	}

	// Readers: Get addresses and aliases
	for i := 0; i < numReaders; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for {
				select {
				case <-stop:
					return
				default:
					domain := fmt.Sprintf("domain-%d.com", rand.Intn(100))
					cache.GetAddresses(domain)

					alias := fmt.Sprintf("alias-%d.com", rand.Intn(100))
					cache.GetAliases(alias)
					cache.GetTargetChain(alias)

					time.Sleep(time.Millisecond * 5)
				}
			}
		}(i)
	}

	// Cleaner: Periodically cleanup
	wg.Add(1)
	go func() {
		defer wg.Done()
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-stop:
				return
			case <-ticker.C:
				cache.EvictExpiredEntries()
			}
		}
	}()

	// Run for a while
	time.Sleep(duration)
	close(stop)
	wg.Wait()

	// Final verification: Wait for TTL to expire and ensure cache is empty
	time.Sleep(time.Duration(ttl)*time.Second + 100*time.Millisecond)
	cache.EvictExpiredEntries()

	addrCount, aliasCount := cache.Stats()
	if addrCount != 0 || aliasCount != 0 {
		t.Errorf("expected empty cache after TTL expiration, got %d addresses, %d aliases", addrCount, aliasCount)
	}
}

func TestRecordsCache_LRU(t *testing.T) {
	cache := NewRecordsCache(3)

	// Add 3 domains
	cache.AddAddress("domain1.com", net.ParseIP("1.1.1.1"), 300)
	cache.AddAddress("domain2.com", net.ParseIP("2.2.2.2"), 300)
	cache.AddAddress("domain3.com", net.ParseIP("3.3.3.3"), 300)

	// Add 4th domain - should evict domain1
	cache.AddAddress("domain4.com", net.ParseIP("4.4.4.4"), 300)

	// domain1 should be evicted
	addrs := cache.GetAddresses("domain1.com")
	if len(addrs) != 0 {
		t.Errorf("expected domain1 to be evicted, but got %d addresses", len(addrs))
	}

	// domain2, domain3, domain4 should still exist
	for _, domain := range []string{"domain2.com", "domain3.com", "domain4.com"} {
		addrs := cache.GetAddresses(domain)
		if len(addrs) != 1 {
			t.Errorf("expected %s to have 1 address, got %d", domain, len(addrs))
		}
	}
}

func TestRecordsCache_NoDuplicateAddressForSameDomain(t *testing.T) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// First addition should return true (new entry)
	if !cache.AddAddress("example.com", ip, 300) {
		t.Error("first AddAddress should return true for new entry")
	}

	// Second addition with same domain+IP should return false (already cached)
	if cache.AddAddress("example.com", ip, 300) {
		t.Error("second AddAddress should return false for existing valid entry")
	}

	// Third addition should also return false
	if cache.AddAddress("example.com", ip, 300) {
		t.Error("third AddAddress should return false for existing valid entry")
	}

	// Verify only one address is stored
	addrs := cache.GetAddresses("example.com")
	if len(addrs) != 1 {
		t.Errorf("expected 1 address, got %d", len(addrs))
	}
}

func TestRecordsCache_SelfReferentialAliasIgnored(t *testing.T) {
	cache := NewRecordsCache(10000)

	// Add self-referential alias - should be ignored
	cache.AddAlias("example.com", "example.com", 300)

	// GetAliases should return only the domain itself, not a duplicate
	aliases := cache.GetAliases("example.com")
	if len(aliases) != 1 {
		t.Errorf("expected 1 alias (just the domain), got %d: %v", len(aliases), aliases)
	}
	if aliases[0] != "example.com" {
		t.Errorf("expected alias to be example.com, got %s", aliases[0])
	}

	// Stats should show 0 aliases (self-ref was ignored)
	_, aliasCount := cache.Stats()
	if aliasCount != 0 {
		t.Errorf("expected 0 aliases in stats (self-ref ignored), got %d", aliasCount)
	}
}

func TestRecordsCache_CNAMEChainResolution(t *testing.T) {
	// Simulates a real DNS response for: test97.svc.kurb.me -> vps-1.node.kurb.me -> 77.90.40.223
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("77.90.40.223")

	// Step 1: A record arrives first (vps-1.node.kurb.me -> IP)
	// This should be a new entry
	if !cache.AddAddress("vps-1.node.kurb.me", ip, 300) {
		t.Error("A record: first AddAddress should return true")
	}

	// Step 2: CNAME record arrives (test97.svc.kurb.me -> vps-1.node.kurb.me)
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// Step 3: When processing CNAME, we also cache the resolved address for the alias
	// This is a DIFFERENT domain, so it should return true
	if !cache.AddAddress("test97.svc.kurb.me", ip, 300) {
		t.Error("CNAME resolution: AddAddress for alias domain should return true (different domain)")
	}

	// Step 4: Second lookup - everything should be cached
	// A record for vps-1.node.kurb.me - already cached
	if cache.AddAddress("vps-1.node.kurb.me", ip, 300) {
		t.Error("second lookup: A record AddAddress should return false (already cached)")
	}

	// CNAME record - alias already exists, address already cached
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	if cache.AddAddress("test97.svc.kurb.me", ip, 300) {
		t.Error("second lookup: CNAME AddAddress should return false (already cached)")
	}

	// Step 5: Third lookup - still cached
	if cache.AddAddress("vps-1.node.kurb.me", ip, 300) {
		t.Error("third lookup: should still be cached")
	}
	if cache.AddAddress("test97.svc.kurb.me", ip, 300) {
		t.Error("third lookup: alias should still be cached")
	}

	// Verify GetAliases returns correct chain
	aliases := cache.GetAliases("vps-1.node.kurb.me")
	expected := map[string]bool{
		"vps-1.node.kurb.me": true,
		"test97.svc.kurb.me": true,
	}
	if len(aliases) != len(expected) {
		t.Errorf("expected %d aliases, got %d: %v", len(expected), len(aliases), aliases)
	}
	for _, alias := range aliases {
		if !expected[alias] {
			t.Errorf("unexpected alias: %s", alias)
		}
	}
}

func TestRecordsCache_CNAMEProcessingBug(t *testing.T) {
	// This test simulates the exact bug scenario from the logs:
	// First nslookup:
	//   Adding 77.90.40.223 (domain: vps-1.node.kurb.me, alias: vps-1.node.kurb.me)
	//   Adding 77.90.40.223 (domain: vps-1.node.kurb.me, alias: test97.svc.kurb.me)
	// Second nslookup:
	//   Adding 77.90.40.223 (CNAME: test97.svc.kurb.me -> vps-1.node.kurb.me, alias: test97.svc.kurb.me)
	//   ^^^ This should NOT happen - it's a duplicate!

	cache := NewRecordsCache(10000)
	ip := net.ParseIP("77.90.40.223")

	// === First nslookup ===

	// Process A record: vps-1.node.kurb.me -> 77.90.40.223
	result1 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if !result1 {
		t.Error("1st lookup, A record: should return true (new entry)")
	}

	// Process CNAME record: test97.svc.kurb.me -> vps-1.node.kurb.me
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// In processCNAMERecord, for each alias, it calls AddAddress(domain, ...)
	// where domain is the CNAME source (test97.svc.kurb.me)
	result2 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if !result2 {
		t.Error("1st lookup, CNAME processing: should return true (new domain+IP combo)")
	}

	// === Second nslookup ===

	// Process A record again
	result3 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result3 {
		t.Error("2nd lookup, A record: should return false (already cached)")
	}

	// Process CNAME record again
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// This is the bug! On second lookup, this should return false
	result4 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if result4 {
		t.Error("2nd lookup, CNAME processing: should return false (already cached) - THIS IS THE BUG!")
	}

	// === Third nslookup ===
	result5 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result5 {
		t.Error("3rd lookup, A record: should return false")
	}

	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	result6 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if result6 {
		t.Error("3rd lookup, CNAME processing: should return false")
	}
}

func TestRecordsCache_CNAMEBeforeARecord(t *testing.T) {
	// Test when CNAME record is processed BEFORE A record in DNS response
	// This can happen depending on DNS server ordering

	cache := NewRecordsCache(10000)
	ip := net.ParseIP("77.90.40.223")

	// === First nslookup (CNAME first, then A) ===

	// Process CNAME record first: test97.svc.kurb.me -> vps-1.node.kurb.me
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// At this point, GetAddresses("vps-1.node.kurb.me") returns empty
	// So processCNAMERecord would return early (no addresses yet)

	// Then A record arrives: vps-1.node.kurb.me -> 77.90.40.223
	result1 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if !result1 {
		t.Error("1st lookup, A record: should return true (new entry)")
	}

	// Now GetAliases("vps-1.node.kurb.me") should include test97.svc.kurb.me
	aliases := cache.GetAliases("vps-1.node.kurb.me")
	if len(aliases) != 2 {
		t.Errorf("expected 2 aliases, got %d: %v", len(aliases), aliases)
	}

	// === Second nslookup ===
	// CNAME first
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	// Then A record
	result2 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result2 {
		t.Error("2nd lookup, A record: should return false (already cached)")
	}
}

func TestRecordsCache_AliasAddressCachingBug(t *testing.T) {
	// This test reproduces the ACTUAL bug when A record comes BEFORE CNAME in response:
	// Both are processed in first lookup, and both are cached properly.

	cache := NewRecordsCache(10000)
	ip := net.ParseIP("77.90.40.223")

	// === First nslookup (A record first, then CNAME) ===

	// Step 1: A record processed first
	result1 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if !result1 {
		t.Error("1st: A record should return true")
	}

	// Step 2: CNAME record processed
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	// processCNAMERecord gets addresses and caches for alias domain
	result2 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if !result2 {
		t.Error("1st: CNAME AddAddress should return true (first time for this domain)")
	}

	// === Second nslookup ===
	result3 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result3 {
		t.Error("2nd: A record should return false (cached)")
	}

	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	result4 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if result4 {
		t.Error("2nd: CNAME AddAddress should return false (cached)")
	}
}

func TestRecordsCache_CNAMEBeforeARecordFixed(t *testing.T) {
	// Tests the FIX for the CNAME-before-A-record bug.
	//
	// The fix: collectIPSetEntries now calls AddAddress for all aliases,
	// ensuring they're cached even when CNAME record is processed first.

	cache := NewRecordsCache(10000)
	ip := net.ParseIP("77.90.40.223")

	// === First nslookup (CNAME first, then A) ===

	// Step 1: CNAME record processed first
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// processCNAMERecord: GetAddresses(target) -> EMPTY, early return
	addrs := cache.GetAddresses("vps-1.node.kurb.me")
	if len(addrs) != 0 {
		t.Errorf("expected 0 addresses before A record, got %d", len(addrs))
	}

	// Step 2: A record processed
	result1 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if !result1 {
		t.Error("1st: A record should return true")
	}

	// THE FIX: collectIPSetEntries now caches addresses for ALL aliases
	// Simulate what collectIPSetEntries does after the fix:
	aliases := cache.GetAliases("vps-1.node.kurb.me")
	for _, alias := range aliases {
		if alias != "vps-1.node.kurb.me" {
			cache.AddAddress(alias, ip, 300) // This is the fix!
		}
	}

	// === Second nslookup ===

	// CNAME processed first
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)

	// Now GetAddresses returns the IP
	addrs = cache.GetAddresses("vps-1.node.kurb.me")
	if len(addrs) != 1 {
		t.Errorf("expected 1 address, got %d", len(addrs))
	}

	// processCNAMERecord calls AddAddress(alias, ip, ttl)
	// WITH THE FIX: It should return FALSE because alias WAS cached by collectIPSetEntries
	result2 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if result2 {
		t.Error("2nd: CNAME AddAddress should return false (cached by fix)")
	}

	// Then A record
	result3 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result3 {
		t.Error("2nd: A record should return false (cached)")
	}

	// === Third nslookup ===
	cache.AddAlias("test97.svc.kurb.me", "vps-1.node.kurb.me", 300)
	result4 := cache.AddAddress("test97.svc.kurb.me", ip, 300)
	if result4 {
		t.Error("3rd: CNAME AddAddress should return false")
	}
	result5 := cache.AddAddress("vps-1.node.kurb.me", ip, 300)
	if result5 {
		t.Error("3rd: A record should return false")
	}
}
