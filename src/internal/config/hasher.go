package config

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

const hashCacheTTL = 5 * time.Minute

// KeeneticClientInterface defines minimal interface for DNS server retrieval
// This avoids circular dependency with domain package
type KeeneticClientInterface interface {
	GetDNSServers() ([]keenetic.DnsServerInfo, error)
}

// ConfigHasher calculates MD5 hash of configuration state
// It acts as a DI component that maintains cached current hash and active hash
type ConfigHasher struct {
	configPath     string
	keeneticClient KeeneticClientInterface

	// Current hash (from config file) with caching
	currentHash      string
	currentHashTime  time.Time

	// Active hash (from running service)
	activeHash string

	mu sync.RWMutex
}

// NewConfigHasher creates a new config hasher
func NewConfigHasher(configPath string) *ConfigHasher {
	return &ConfigHasher{
		configPath: configPath,
	}
}

// SetKeeneticClient sets the Keenetic client for DNS server tracking
func (h *ConfigHasher) SetKeeneticClient(client KeeneticClientInterface) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.keeneticClient = client
}

// GetCurrentConfigHash returns cached hash of current config file
// Automatically calls UpdateCurrentConfigHash() on cache miss
func (h *ConfigHasher) GetCurrentConfigHash() (string, error) {
	h.mu.RLock()
	if time.Since(h.currentHashTime) < hashCacheTTL && h.currentHash != "" {
		hash := h.currentHash
		h.mu.RUnlock()
		return hash, nil
	}
	h.mu.RUnlock()

	// Cache miss - recalculate
	return h.UpdateCurrentConfigHash()
}

// UpdateCurrentConfigHash recalculates config hash and resets cache
// Always recalculates regardless of cache state (called after config changes)
func (h *ConfigHasher) UpdateCurrentConfigHash() (string, error) {
	h.mu.Lock()
	defer h.mu.Unlock()

	// Load config from file
	cfg, err := LoadConfig(h.configPath)
	if err != nil {
		return "", fmt.Errorf("failed to load config: %w", err)
	}

	// Calculate hash
	hash, err := h.calculateHashForConfig(cfg)
	if err != nil {
		return "", fmt.Errorf("failed to calculate hash: %w", err)
	}

	// Update cache
	h.currentHash = hash
	h.currentHashTime = time.Now()

	return hash, nil
}

// GetKeenPbrActiveConfigHash returns hash of config that was active when service started
func (h *ConfigHasher) GetKeenPbrActiveConfigHash() string {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return h.activeHash
}

// SetKeenPbrActiveConfigHash sets the hash of config when service starts
func (h *ConfigHasher) SetKeenPbrActiveConfigHash(hash string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.activeHash = hash
}

// GetDnsmasqActiveConfigHash queries dnsmasq for the active config hash
// It performs a DNS lookup for config-md5.keen-pbr.internal against 127.0.0.1:53
// and extracts the hash from the CNAME response (e.g., <hash>.value.keen-pbr.internal)
// Returns empty string if DNS query fails or hash cannot be extracted
func (h *ConfigHasher) GetDnsmasqActiveConfigHash() string {
	// Create custom resolver pointing to local dnsmasq
	resolver := &net.Resolver{
		PreferGo: true,
		Dial: func(ctx context.Context, network, address string) (net.Conn, error) {
			d := net.Dialer{
				Timeout: 2 * time.Second,
			}
			return d.DialContext(ctx, "udp", "127.0.0.1:53")
		},
	}

	// Perform CNAME lookup with 2-second timeout
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	cname, err := resolver.LookupCNAME(ctx, "config-md5.keen-pbr.internal")
	if err != nil {
		// DNS query failed or domain doesn't exist
		return ""
	}

	// Parse CNAME response: expect <hash>.value.keen-pbr.internal.
	// Remove trailing dot if present
	cname = strings.TrimSuffix(cname, ".")

	// Check if it matches the expected pattern
	if !strings.HasSuffix(cname, ".value.keen-pbr.internal") {
		// Invalid CNAME format
		return ""
	}

	// Extract hash from the beginning
	hash := strings.TrimSuffix(cname, ".value.keen-pbr.internal")

	// Validate that hash is not empty and looks like an MD5 (32 hex characters)
	if len(hash) != 32 {
		return ""
	}

	// Verify it's all hexadecimal
	for _, c := range hash {
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
			return ""
		}
	}

	return strings.ToLower(hash)
}

// calculateHashForConfig generates MD5 hash of entire configuration
// This is the internal method that does the actual hashing
func (h *ConfigHasher) calculateHashForConfig(config *Config) (string, error) {
	// 1. Get used list names from all ipsets
	usedLists := h.getUsedListNamesForConfig(config)

	// 2. Get DNS servers if Keenetic client is available and use_keenetic_dns is enabled
	var dnsServers []DNSServerHashData
	h.mu.RLock()
	keeneticClient := h.keeneticClient
	h.mu.RUnlock()

	if config.General.UseKeeneticDNS != nil && *config.General.UseKeeneticDNS && keeneticClient != nil {
		servers, err := keeneticClient.GetDNSServers()
		if err == nil && len(servers) > 0 {
			// Convert to hashable format
			dnsServers = make([]DNSServerHashData, len(servers))
			for i, server := range servers {
				dnsServers[i] = DNSServerHashData{
					Type:     string(server.Type),
					Proxy:    server.Proxy,
					Endpoint: server.Endpoint,
					Port:     server.Port,
					Domain:   server.Domain,
				}
			}
		}
	}

	// 3. Build hashable structure
	hashData := &ConfigHashData{
		General:    config.General,
		IPSets:     h.buildIPSetHashDataForConfig(config),
		ListMD5s:   h.calculateListHashesForConfig(config, usedLists),
		DNSServers: dnsServers,
	}

	// 4. Serialize to JSON (sorted keys for determinism)
	jsonBytes, err := json.Marshal(hashData)
	if err != nil {
		return "", fmt.Errorf("failed to marshal config data: %w", err)
	}

	// 5. Calculate MD5
	hash := md5.Sum(jsonBytes)
	return hex.EncodeToString(hash[:]), nil
}

// getUsedListNamesForConfig returns set of list names actually used in ipsets
func (h *ConfigHasher) getUsedListNamesForConfig(config *Config) map[string]bool {
	used := make(map[string]bool)
	for _, ipset := range config.IPSets {
		for _, listName := range ipset.Lists {
			used[listName] = true
		}
	}
	return used
}

// buildIPSetHashDataForConfig creates hashable representation of ipsets
func (h *ConfigHasher) buildIPSetHashDataForConfig(config *Config) []*IPSetHashData {
	result := make([]*IPSetHashData, len(config.IPSets))
	for i, ipset := range config.IPSets {
		result[i] = &IPSetHashData{
			IPSetName:           ipset.IPSetName,
			Lists:               sortedStrings(ipset.Lists),
			IPVersion:           ipset.IPVersion,
			FlushBeforeApplying: ipset.FlushBeforeApplying,
			Routing:             ipset.Routing,
			IPTablesRules:       ipset.IPTablesRules,
		}
	}
	return result
}

// calculateListHashesForConfig generates MD5 for each used list
func (h *ConfigHasher) calculateListHashesForConfig(config *Config, usedLists map[string]bool) map[string]string {
	hashes := make(map[string]string)

	for _, list := range config.Lists {
		if !usedLists[list.ListName] {
			continue // Skip unused lists
		}

		hash, err := h.calculateListHashForConfig(config, list)
		if err != nil {
			// Use error as hash to indicate problem
			hashes[list.ListName] = fmt.Sprintf("error:%v", err)
		} else {
			hashes[list.ListName] = hash
		}
	}

	return hashes
}

// calculateListHashForConfig calculates hash for a single list
func (h *ConfigHasher) calculateListHashForConfig(config *Config, list *ListSource) (string, error) {
	switch list.Type() {
	case "url":
		return h.hashListFileForConfig(config, list)
	case "file":
		return h.hashListFileForConfig(config, list)
	case "hosts":
		return h.hashInlineHosts(list)
	default:
		return "", fmt.Errorf("unknown list type")
	}
}

// hashListFileForConfig calculates MD5 of a file-based list
func (h *ConfigHasher) hashListFileForConfig(config *Config, list *ListSource) (string, error) {
	path, err := list.GetAbsolutePath(config)
	if err != nil {
		return "", err
	}

	file, err := os.Open(path)
	if err != nil {
		return "", fmt.Errorf("failed to open list file: %w", err)
	}
	defer file.Close()

	hash := md5.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", fmt.Errorf("failed to hash list file: %w", err)
	}

	return hex.EncodeToString(hash.Sum(nil)), nil
}

// hashInlineHosts calculates MD5 of inline hosts array
func (h *ConfigHasher) hashInlineHosts(list *ListSource) (string, error) {
	// Sort for deterministic hashing
	sorted := make([]string, len(list.Hosts))
	copy(sorted, list.Hosts)
	sort.Strings(sorted)

	// Serialize and hash
	data, err := json.Marshal(sorted)
	if err != nil {
		return "", err
	}

	hash := md5.Sum(data)
	return hex.EncodeToString(hash[:]), nil
}

// Helper types for hashing

// ConfigHashData represents the structure used for hashing
type ConfigHashData struct {
	General    *GeneralConfig      `json:"general"`
	IPSets     []*IPSetHashData    `json:"ipsets"`
	ListMD5s   map[string]string   `json:"list_md5s"`
	DNSServers []DNSServerHashData `json:"dns_servers,omitempty"` // From Keenetic if enabled
}

// IPSetHashData represents hashable IPSet configuration
type IPSetHashData struct {
	IPSetName           string          `json:"ipset_name"`
	Lists               []string        `json:"lists"`
	IPVersion           IpFamily        `json:"ip_version"`
	FlushBeforeApplying bool            `json:"flush_before_applying"`
	Routing             *RoutingConfig  `json:"routing,omitempty"`
	IPTablesRules       []*IPTablesRule `json:"iptables_rules,omitempty"`
}

// DNSServerHashData represents hashable DNS server information
type DNSServerHashData struct {
	Type     string  `json:"type"`               // "IP4", "IP6", "DoT", "DoH"
	Proxy    string  `json:"proxy"`              // Proxy IP address
	Endpoint string  `json:"endpoint"`           // Endpoint (IP/SNI/URI)
	Port     string  `json:"port"`               // Port
	Domain   *string `json:"domain,omitempty"`   // Domain scope
}

// sortedStrings returns a sorted copy of a string slice
func sortedStrings(s []string) []string {
	result := make([]string, len(s))
	copy(result, s)
	sort.Strings(result)
	return result
}
