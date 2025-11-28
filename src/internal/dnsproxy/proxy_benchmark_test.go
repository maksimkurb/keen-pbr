package dnsproxy

import (
	"fmt"
	"net"
	"net/netip"
	"sync/atomic"
	"testing"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
	"github.com/miekg/dns"
)

// MockIPSetManager implements domain.IPSetManager for benchmarking
type MockIPSetManager struct {
	AddCount uint64
}

func (m *MockIPSetManager) Create(name string, family config.IPFamily) error { return nil }
func (m *MockIPSetManager) Flush(name string) error                          { return nil }
func (m *MockIPSetManager) Import(ipsetCfg *config.IPSetConfig, networks []netip.Prefix) error {
	return nil
}
func (m *MockIPSetManager) CreateIfAbsent(cfg *config.Config) error { return nil }
func (m *MockIPSetManager) BatchAddWithTTL(entries []networking.IPSetEntry) error {
	atomic.AddUint64(&m.AddCount, uint64(len(entries)))
	return nil
}

// MockKeeneticClient implements domain.KeeneticClient for benchmarking
type MockKeeneticClient struct{}

func (m *MockKeeneticClient) GetVersion() (*keenetic.KeeneticVersion, error) {
	return &keenetic.KeeneticVersion{}, nil
}
func (m *MockKeeneticClient) GetRawVersion() (string, error) { return "1.0.0", nil }
func (m *MockKeeneticClient) GetInterfaces() (map[string]keenetic.Interface, error) {
	return map[string]keenetic.Interface{}, nil
}
func (m *MockKeeneticClient) GetDNSServers() ([]keenetic.DNSServerInfo, error) {
	return []keenetic.DNSServerInfo{}, nil
}

// StartMockUpstream starts a mock DNS upstream server
func StartMockUpstream(t testing.TB) (string, func()) {
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to listen packet: %v", err)
	}

	server := &dns.Server{
		PacketConn: pc,
		Handler: dns.HandlerFunc(func(w dns.ResponseWriter, r *dns.Msg) {
			m := new(dns.Msg)
			m.SetReply(r)
			m.Authoritative = true
			m.RecursionAvailable = true

			// Add a dummy A record
			rr, _ := dns.NewRR(fmt.Sprintf("%s 60 IN A 1.2.3.4", r.Question[0].Name))
			m.Answer = append(m.Answer, rr)

			w.WriteMsg(m)
		}),
	}

	go func() {
		server.ActivateAndServe()
	}()

	return pc.LocalAddr().String(), func() {
		server.Shutdown()
	}
}

// BenchmarkDNSProxy_NotInIPSet benchmarks DNS queries for domains NOT in any IPSet
// This should have ZERO allocations for IPSet operations (optimal path)
func BenchmarkDNSProxy_NotInIPSet(b *testing.B) {
	log.DisableLogs()

	// Setup mock upstream
	upstreamAddr, stopUpstream := StartMockUpstream(b)
	defer stopUpstream()

	// Setup proxy config
	proxyPort := 15354
	cfg := ProxyConfig{
		ListenAddr:                 "127.0.0.1",
		ListenPort:                 uint16(proxyPort),
		Upstreams:                  []string{"udp://" + upstreamAddr},
		DropAAAA:                   true,
		IPSetEntryAdditionalTTLSec: 60,
		MaxCacheDomains:            1000,
	}

	appCfg := &config.Config{
		General: &config.GeneralConfig{
			DNSServer: &config.DNSServerConfig{
				ListenAddr:      "127.0.0.1",
				ListenPort:      uint16(proxyPort),
				Upstreams:       []string{"udp://" + upstreamAddr},
				CacheMaxDomains: 1000,
			},
		},
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"example.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: config.Ipv4,
				Lists:     []string{"test-list"},
				Routing: &config.RoutingConfig{
					FwMark:         1,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
	}

	proxy, err := NewDNSProxy(cfg, &MockKeeneticClient{}, &MockIPSetManager{}, appCfg)
	if err != nil {
		b.Fatalf("failed to create proxy: %v", err)
	}

	if err := proxy.Start(); err != nil {
		b.Fatalf("failed to start proxy: %v", err)
	}
	defer proxy.Stop()

	time.Sleep(100 * time.Millisecond)

	client := new(dns.Client)
	client.Timeout = 2 * time.Second
	targetAddr := fmt.Sprintf("127.0.0.1:%d", proxyPort)

	b.ResetTimer()
	b.ReportAllocs()

	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			i++
			m := new(dns.Msg)
			// Use domains NOT in IPSet (google.com not in list)
			domain := fmt.Sprintf("sub%d.google.com.", i%100)
			m.SetQuestion(domain, dns.TypeA)

			_, _, err := client.Exchange(m, targetAddr)
			if err != nil {
				// Ignore errors in benchmark
			}
		}
	})
}

// BenchmarkDNSProxy_InIPSet benchmarks DNS queries for domains IN IPSet (cache miss)
// This will have allocations for IPSet operations (expected)
func BenchmarkDNSProxy_InIPSet(b *testing.B) {
	log.DisableLogs()

	upstreamAddr, stopUpstream := StartMockUpstream(b)
	defer stopUpstream()

	proxyPort := 15355 // Different port
	cfg := ProxyConfig{
		ListenAddr:                 "127.0.0.1",
		ListenPort:                 uint16(proxyPort),
		Upstreams:                  []string{"udp://" + upstreamAddr},
		DropAAAA:                   true,
		IPSetEntryAdditionalTTLSec: 60,
		MaxCacheDomains:            10, // Small cache to force misses
	}

	appCfg := &config.Config{
		General: &config.GeneralConfig{
			DNSServer: &config.DNSServerConfig{
				ListenAddr:      "127.0.0.1",
				ListenPort:      uint16(proxyPort),
				Upstreams:       []string{"udp://" + upstreamAddr},
				CacheMaxDomains: 10,
			},
		},
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"example.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: config.Ipv4,
				Lists:     []string{"test-list"},
				Routing: &config.RoutingConfig{
					FwMark:         1,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
	}

	proxy, err := NewDNSProxy(cfg, &MockKeeneticClient{}, &MockIPSetManager{}, appCfg)
	if err != nil {
		b.Fatalf("failed to create proxy: %v", err)
	}

	if err := proxy.Start(); err != nil {
		b.Fatalf("failed to start proxy: %v", err)
	}
	defer proxy.Stop()

	time.Sleep(100 * time.Millisecond)

	client := new(dns.Client)
	client.Timeout = 2 * time.Second
	targetAddr := fmt.Sprintf("127.0.0.1:%d", proxyPort)

	b.ResetTimer()
	b.ReportAllocs()

	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			i++
			m := new(dns.Msg)
			// Use domains IN IPSet with unique subdomains to force cache misses
			domain := fmt.Sprintf("unique%d.example.com.", i%10000)
			m.SetQuestion(domain, dns.TypeA)

			_, _, err := client.Exchange(m, targetAddr)
			if err != nil {
				// Ignore errors in benchmark
			}
		}
	})
}

// BenchmarkDNSProxy is the legacy benchmark (mostly cache hits)
func BenchmarkDNSProxy(b *testing.B) {
	log.DisableLogs()

	upstreamAddr, stopUpstream := StartMockUpstream(b)
	defer stopUpstream()

	proxyPort := 15356
	cfg := ProxyConfig{
		ListenAddr:                 "127.0.0.1",
		ListenPort:                 uint16(proxyPort),
		Upstreams:                  []string{"udp://" + upstreamAddr},
		DropAAAA:                   true,
		IPSetEntryAdditionalTTLSec: 60,
		MaxCacheDomains:            1000,
	}

	appCfg := &config.Config{
		General: &config.GeneralConfig{
			DNSServer: &config.DNSServerConfig{
				ListenAddr:      "127.0.0.1",
				ListenPort:      uint16(proxyPort),
				Upstreams:       []string{"udp://" + upstreamAddr},
				CacheMaxDomains: 1000,
			},
		},
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"example.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "test-ipset",
				IPVersion: config.Ipv4,
				Lists:     []string{"test-list"},
				Routing: &config.RoutingConfig{
					FwMark:         1,
					IPRouteTable:   100,
					IPRulePriority: 100,
				},
			},
		},
	}

	proxy, err := NewDNSProxy(cfg, &MockKeeneticClient{}, &MockIPSetManager{}, appCfg)
	if err != nil {
		b.Fatalf("failed to create proxy: %v", err)
	}

	if err := proxy.Start(); err != nil {
		b.Fatalf("failed to start proxy: %v", err)
	}
	defer proxy.Stop()

	time.Sleep(100 * time.Millisecond)

	client := new(dns.Client)
	client.Timeout = 2 * time.Second
	targetAddr := fmt.Sprintf("127.0.0.1:%d", proxyPort)

	b.ResetTimer()
	b.ReportAllocs()

	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			i++
			m := new(dns.Msg)
			domain := fmt.Sprintf("sub%d.example.com.", i%100)
			m.SetQuestion(domain, dns.TypeA)

			_, _, err := client.Exchange(m, targetAddr)
			if err != nil {
				// Ignore errors
			}
		}
	})
}
