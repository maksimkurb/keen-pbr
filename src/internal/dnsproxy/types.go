package dnsproxy

import (
	"net"
	"runtime"
	"strings"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

const (
	// Network protocol identifiers
	networkUDP = "udp"
	networkTCP = "tcp"

	// Timeout durations
	tcpConnectionTimeout = 15 * time.Second // TCP connection total timeout - increased for upstream+operations
	upstreamQueryTimeout = 5 * time.Second  // Timeout for upstream DNS queries - should be longest

	// EvictExpiredEntries intervals
	cacheCleanupInterval = 1 * time.Minute // How often to clean up expired cache entries

	// Size limits
	maxDNSMessageSize = 4096 // Maximum DNS message size for TCP to prevent DoS

	// IP address prefix lengths
	ipv4PrefixLen = 32  // /32 for single IPv4 addresses
	ipv6PrefixLen = 128 // /128 for single IPv6 addresses
)

// ProxyConfig contains configuration for the DNS proxy.
type ProxyConfig struct {
	// ListenAddr is the address to listen on (default: [::])
	ListenAddr string

	// ListenPort is the port to listen on (default: 15353)
	ListenPort uint16

	// Upstreams is the list of upstream DNS URLs
	// Supported: keenetic://, udp://ip:port, doh://host/path
	Upstreams []string

	// DropAAAA drops AAAA (IPv6) responses (default: true)
	DropAAAA bool

	// IPSetEntryAdditionalTTLSec is added to DNS record TTL to determine IPSet entry lifetime (default: 7200 = 2 hours)
	IPSetEntryAdditionalTTLSec uint32

	// ListedDomainsDNSCacheTTLSec is the TTL to use for domains found by matcher in recordsCache (default: 30)
	// This allows clients to forget DNS fast for domains that are in the matchers, while other domains keep original TTL
	ListedDomainsDNSCacheTTLSec uint32

	// MaxCacheDomains is the maximum number of domains to cache (default: 10000)
	MaxCacheDomains int

	// WorkerPoolSize is the number of UDP worker goroutines (default: runtime.NumCPU() * 2)
	WorkerPoolSize int

	// TCPWorkerPoolSize is the number of TCP worker goroutines (default: runtime.NumCPU())
	TCPWorkerPoolSize int

	// WorkQueueSize is the UDP request queue depth (default: WorkerPoolSize * 8)
	WorkQueueSize int

	// TCPQueueSize is the TCP request queue depth (default: TCPWorkerPoolSize * 4)
	TCPQueueSize int

	// MaxTCPConnections is the maximum concurrent TCP connections (default: 100)
	MaxTCPConnections int
}

// udpRequest represents a UDP DNS request to be processed by a worker.
type udpRequest struct {
	conn       *net.UDPConn
	clientAddr *net.UDPAddr
	buf        []byte // From pool, worker must return to pool after processing
	n          int    // Actual bytes read
}

// tcpRequest represents a TCP DNS connection to be processed by a worker.
type tcpRequest struct {
	conn net.Conn
}

// ProxyConfigFromAppConfig creates a ProxyConfig from the application config.
func ProxyConfigFromAppConfig(cfg *config.Config) ProxyConfig {
	if cfg.General.DNSServer == nil {
		return ProxyConfig{}
	}
	return ProxyConfig{
		ListenAddr:                  cfg.General.DNSServer.ListenAddr,
		ListenPort:                  cfg.General.DNSServer.ListenPort,
		Upstreams:                   cfg.General.DNSServer.Upstreams,
		DropAAAA:                    cfg.General.DNSServer.DropAAAA,
		IPSetEntryAdditionalTTLSec:  cfg.General.DNSServer.IPSetEntryAdditionalTTLSec,
		ListedDomainsDNSCacheTTLSec: cfg.General.DNSServer.ListedDomainsDNSCacheTTLSec,
		MaxCacheDomains:             cfg.General.DNSServer.CacheMaxDomains,
	}
}

// normalizeDomain removes the trailing dot from a domain name.
func normalizeDomain(domain string) string {
	if len(domain) > 0 && domain[len(domain)-1] == '.' {
		return domain[:len(domain)-1]
	}
	return strings.ToLower(domain)
}

// getDefaultWorkerCount returns the default number of workers based on CPU count.
func getDefaultWorkerCount() int {
	return runtime.NumCPU() * 2
}
