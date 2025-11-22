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
	cache := NewRecordsCache()

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
	cache := NewRecordsCache()

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
	cache := NewRecordsCache()

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
	cache := NewRecordsCache()

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
	cache := NewRecordsCache()

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
	cache := NewRecordsCache()
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
