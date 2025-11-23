package dnsproxy

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
	cache.Cleanup()

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
				cache.Cleanup()
			}
		}
	}()

	// Run for a while
	time.Sleep(duration)
	close(stop)
	wg.Wait()

	// Final verification: Wait for TTL to expire and ensure cache is empty
	time.Sleep(time.Duration(ttl)*time.Second + 100*time.Millisecond)
	cache.Cleanup()

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

// Benchmarks

func BenchmarkRecordsCache_AddAddress(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%1000)
		cache.AddAddress(domain, ip, 300)
	}
}

func BenchmarkRecordsCache_GetAddresses(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate cache
	for i := 0; i < 1000; i++ {
		domain := fmt.Sprintf("domain-%d.com", i)
		cache.AddAddress(domain, ip, 300)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%1000)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_AddAlias(b *testing.B) {
	cache := NewRecordsCache(10000)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("alias-%d.com", i%1000)
		target := fmt.Sprintf("target-%d.com", i%100)
		cache.AddAlias(domain, target, 300)
	}
}

func BenchmarkRecordsCache_GetAliases(b *testing.B) {
	cache := NewRecordsCache(10000)

	// Pre-populate with CNAME chains
	for i := 0; i < 100; i++ {
		target := fmt.Sprintf("target-%d.com", i)
		for j := 0; j < 5; j++ {
			alias := fmt.Sprintf("alias-%d-%d.com", i, j)
			cache.AddAlias(alias, target, 300)
		}
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		target := fmt.Sprintf("target-%d.com", i%100)
		cache.GetAliases(target)
	}
}

func BenchmarkRecordsCache_Cleanup(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate cache with mix of expired and valid entries
	for i := 0; i < 500; i++ {
		domain := fmt.Sprintf("domain-%d.com", i)
		cache.AddAddress(domain, ip, 300)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cache.Cleanup()
	}
}

func BenchmarkRecordsCache_MemoryUsage(b *testing.B) {
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		cache := NewRecordsCache(10000)

		// Add 1000 domains with 2 addresses each
		for j := 0; j < 1000; j++ {
			domain := fmt.Sprintf("domain-%d.example.com", j)
			cache.AddAddress(domain, net.ParseIP(fmt.Sprintf("1.2.%d.%d", j/256, j%256)), 300)
			cache.AddAddress(domain, net.ParseIP(fmt.Sprintf("2.2.%d.%d", j/256, j%256)), 300)
		}

		// Add 500 aliases
		for j := 0; j < 500; j++ {
			alias := fmt.Sprintf("www-%d.example.com", j)
			target := fmt.Sprintf("domain-%d.example.com", j)
			cache.AddAlias(alias, target, 300)
		}
	}
}

func BenchmarkRecordsCache_ConcurrentReads(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate cache
	for i := 0; i < 1000; i++ {
		domain := fmt.Sprintf("domain-%d.com", i)
		cache.AddAddress(domain, ip, 300)
	}

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			domain := fmt.Sprintf("domain-%d.com", i%1000)
			cache.GetAddresses(domain)
			i++
		}
	})
}

func BenchmarkRecordsCache_ConcurrentWrites(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			domain := fmt.Sprintf("domain-%d.com", i%1000)
			cache.AddAddress(domain, ip, 300)
			i++
		}
	})
}
