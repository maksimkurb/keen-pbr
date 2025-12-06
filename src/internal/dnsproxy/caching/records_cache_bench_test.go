package caching

import (
	"fmt"
	"math/rand"
	"net"
	"testing"
)

// User-requested benchmarks - cache scenarios

func BenchmarkRecordsCache_CacheMiss(b *testing.B) {
	cache := NewRecordsCache(10000)
	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("nonexistent-%d.com", i)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_FullCache(b *testing.B) {
	cache := NewRecordsCache(1000)
	ip := net.ParseIP("1.2.3.4")

	// Fill to capacity
	for i := 0; i < 1000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%1000)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_AddNewOnFullCache(b *testing.B) {
	cache := NewRecordsCache(1000)
	ip := net.ParseIP("1.2.3.4")

	// Fill to capacity
	for i := 0; i < 1000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("new-%d.com", i)
		cache.AddAddress(domain, ip, 300)
	}
}

func BenchmarkRecordsCache_AddNewOnEmptyCache(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%1000)
		cache.AddAddress(domain, ip, 300)
	}
}

func BenchmarkRecordsCache_GetCachedOnFullCache(b *testing.B) {
	cache := NewRecordsCache(1000)
	ip := net.ParseIP("1.2.3.4")

	// Fill to capacity
	for i := 0; i < 1000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%1000)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_GetCachedOnAlmostEmptyCache(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Add only 10 domains (almost empty)
	for i := 0; i < 10; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.com", i%10)
		cache.GetAddresses(domain)
	}
}

// Comprehensive benchmarks - performance insights

func BenchmarkRecordsCache_GetTargetChain(b *testing.B) {
	cache := NewRecordsCache(10000)

	// Create chains of varying depths (1-5 hops)
	for i := 0; i < 1000; i++ {
		depth := (i % 5) + 1
		for j := 0; j < depth; j++ {
			source := fmt.Sprintf("alias-%d-%d.com", i, j)
			target := fmt.Sprintf("alias-%d-%d.com", i, j+1)
			cache.AddAlias(source, target, 300)
		}
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("alias-%d-0.com", i%1000)
		cache.GetTargetChain(domain)
	}
}

func BenchmarkRecordsCache_ReverseAliasRebuild(b *testing.B) {
	cache := NewRecordsCache(10000)

	// Pre-populate with aliases
	for i := 0; i < 5000; i++ {
		cache.AddAlias(fmt.Sprintf("alias-%d.com", i),
			fmt.Sprintf("target-%d.com", i%100), 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		// Force rebuild by invalidating reverse map
		cache.AddAlias("trigger.com", "new-target.com", 300)

		// This will trigger rebuild
		cache.GetAliases("target-0.com")
	}
}

func BenchmarkRecordsCache_ConcurrentMixed(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate
	for i := 0; i < 1000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
	}

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			op := i % 10
			domain := fmt.Sprintf("domain-%d.com", i%1000)

			switch op {
			case 0, 1, 2, 3, 4, 5: // 60% reads
				cache.GetAddresses(domain)
			case 6, 7: // 20% writes
				cache.AddAddress(domain, ip, 300)
			case 8: // 10% alias ops
				cache.GetAliases(domain)
			case 9: // 10% chain traversal
				cache.GetTargetChain(domain)
			}
			i++
		}
	})
}

func BenchmarkRecordsCache_IPv4vsIPv6(b *testing.B) {
	b.Run("IPv4", func(b *testing.B) {
		cache := NewRecordsCache(10000)
		ip := net.ParseIP("192.168.1.1")
		b.ResetTimer()
		for i := 0; i < b.N; i++ {
			domain := fmt.Sprintf("domain-%d.com", i%1000)
			cache.AddAddress(domain, ip, 300)
		}
	})

	b.Run("IPv6", func(b *testing.B) {
		cache := NewRecordsCache(10000)
		ip := net.ParseIP("2001:db8::1")
		b.ResetTimer()
		for i := 0; i < b.N; i++ {
			domain := fmt.Sprintf("domain-%d.com", i%1000)
			cache.AddAddress(domain, ip, 300)
		}
	})
}

func BenchmarkRecordsCache_CacheHitRatio(b *testing.B) {
	ratios := []int{0, 50, 90, 100}

	for _, hitRatio := range ratios {
		b.Run(fmt.Sprintf("HitRate%d", hitRatio), func(b *testing.B) {
			cache := NewRecordsCache(10000)
			ip := net.ParseIP("1.2.3.4")

			// Pre-populate for hit ratio
			if hitRatio > 0 {
				for i := 0; i < 1000; i++ {
					cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
				}
			}

			b.ResetTimer()
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				var domain string
				if rand.Intn(100) < hitRatio {
					domain = fmt.Sprintf("domain-%d.com", i%1000) // Hit
				} else {
					domain = fmt.Sprintf("miss-%d.com", i) // Miss
				}
				cache.AddAddress(domain, ip, 300)
			}
		})
	}
}

func BenchmarkRecordsCache_DeepCNAMEChains(b *testing.B) {
	depths := []int{1, 5, 10, 20}

	for _, depth := range depths {
		b.Run(fmt.Sprintf("Depth%d", depth), func(b *testing.B) {
			cache := NewRecordsCache(10000)

			// Create chains of specified depth
			for i := 0; i < 100; i++ {
				for j := 0; j < depth; j++ {
					source := fmt.Sprintf("level%d-chain%d.com", j, i)
					target := fmt.Sprintf("level%d-chain%d.com", j+1, i)
					cache.AddAlias(source, target, 300)
				}
			}

			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				target := fmt.Sprintf("level%d-chain%d.com", depth, i%100)
				cache.GetAliases(target)
			}
		})
	}
}

func BenchmarkRecordsCache_LargeScale_10k(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate 10k domains
	for i := 0; i < 10000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.example.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("domain-%d.example.com", i%10000)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_LargeScale_100k(b *testing.B) {
	cache := NewRecordsCache(100000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate 100k domains
	for i := 0; i < 100000; i++ {
		cache.AddAddress(fmt.Sprintf("d%d.example.com", i), ip, 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		domain := fmt.Sprintf("d%d.example.com", i%100000)
		cache.GetAddresses(domain)
	}
}

func BenchmarkRecordsCache_MixedWorkload_ReadHeavy(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate
	for i := 0; i < 1000; i++ {
		cache.AddAddress(fmt.Sprintf("domain-%d.com", i), ip, 300)
		cache.AddAlias(fmt.Sprintf("alias-%d.com", i),
			fmt.Sprintf("domain-%d.com", i), 300)
	}

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if i%10 < 9 { // 90% reads
			cache.GetAddresses(fmt.Sprintf("domain-%d.com", i%1000))
			cache.GetAliases(fmt.Sprintf("domain-%d.com", i%1000))
		} else { // 10% writes
			cache.AddAddress(fmt.Sprintf("domain-%d.com", i%1000), ip, 300)
		}
	}
}

func BenchmarkRecordsCache_MultipleIPsPerDomain(b *testing.B) {
	ipsPerDomain := []int{1, 2, 5, 10}

	for _, numIPs := range ipsPerDomain {
		b.Run(fmt.Sprintf("%dIPs", numIPs), func(b *testing.B) {
			cache := NewRecordsCache(10000)

			// Pre-populate with multiple IPs
			for i := 0; i < 1000; i++ {
				domain := fmt.Sprintf("domain-%d.com", i)
				for j := 0; j < numIPs; j++ {
					ip := net.ParseIP(fmt.Sprintf("1.2.%d.%d", i%256, j))
					cache.AddAddress(domain, ip, 300)
				}
			}

			b.ResetTimer()
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				domain := fmt.Sprintf("domain-%d.com", i%1000)
				cache.GetAddresses(domain)
			}
		})
	}
}

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

func BenchmarkRecordsCache_EvictExpiredEntries(b *testing.B) {
	cache := NewRecordsCache(10000)
	ip := net.ParseIP("1.2.3.4")

	// Pre-populate cache with mix of expired and valid entries
	for i := 0; i < 500; i++ {
		domain := fmt.Sprintf("domain-%d.com", i)
		cache.AddAddress(domain, ip, 300)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cache.EvictExpiredEntries()
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
