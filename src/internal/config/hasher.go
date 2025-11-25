package config

import (
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"sort"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

const hashCacheTTL = 5 * time.Minute

// DNSProvider defines interface for retrieving resolved DNS upstreams
// This allows injecting cached or fresh providers
type DNSProvider interface {
	GetDNSStrings() ([]string, error)
}

// ConfigHasher calculates MD5 hash of configuration state
// It acts as a DI component that maintains cached current hash and active hash
type ConfigHasher struct {
	configPath  string
	dnsProvider DNSProvider

	// Current hash (from config file) with caching
	currentHash     string
	currentHashTime time.Time

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

// SetDNSProvider sets the provider for resolving dynamic upstreams
func (h *ConfigHasher) SetDNSProvider(provider DNSProvider) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.dnsProvider = provider
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
	// Read provider BEFORE acquiring write lock to avoid deadlock
	h.mu.RLock()
	provider := h.dnsProvider
	h.mu.RUnlock()

	h.mu.Lock()
	defer h.mu.Unlock()

	// Load config from file
	cfg, err := LoadConfig(h.configPath)
	if err != nil {
		return "", fmt.Errorf("failed to load config: %w", err)
	}

	// Calculate hash (pass provider to avoid nested lock)
	hash, err := h.calculateHashForConfig(cfg, provider)
	if err != nil {
		return "", fmt.Errorf("failed to calculate hash: %w", err)
	}

	// Update cache
	h.currentHash = hash
	h.currentHashTime = time.Now()

	return hash, nil
}

// CalculateHash calculates hash for a given config object
// Exposed for use by ServiceManager to calculate active hash
func (h *ConfigHasher) CalculateHash(config *Config) (string, error) {
	h.mu.RLock()
	provider := h.dnsProvider
	h.mu.RUnlock()
	return h.calculateHashForConfig(config, provider)
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

// calculateHashForConfig generates MD5 hash of entire configuration
// This is the internal method that does the actual hashing
func (h *ConfigHasher) calculateHashForConfig(config *Config, provider DNSProvider) (string, error) {
	// 1. Get used list names from all ipsets
	usedLists := h.getUsedListNamesForConfig(config)

	// 2. Get DNS servers if provider is available and keenetic:// is in upstreams
	var dnsServers []string

	useKeeneticDNS := false
	if config.General.DNSServer != nil && config.General.DNSServer.Enable {
		for _, upstream := range config.General.DNSServer.Upstreams {
			if upstream == "keenetic://" {
				useKeeneticDNS = true
				break
			}
		}
	}

	if useKeeneticDNS && provider != nil {
		// Use a channel to implement timeout for DNS server retrieval
		type dnsResult struct {
			servers []string
			err     error
		}
		resultCh := make(chan dnsResult, 1)

		// Launch DNS retrieval in a goroutine
		go func() {
			servers, err := provider.GetDNSStrings()
			resultCh <- dnsResult{servers: servers, err: err}
		}()

		// Wait for result with timeout
		select {
		case result := <-resultCh:
			if result.err == nil {
				dnsServers = result.servers
			}
		case <-time.After(5 * time.Second):
			// Timeout: continue without DNS servers
		}
	}

	// Sort DNS servers for determinism
	sortedDNS := make([]string, len(dnsServers))
	copy(sortedDNS, dnsServers)
	sort.Strings(sortedDNS)

	// 3. Build hashable structure
	hashData := &ConfigHashData{
		General:    config.General,
		IPSets:     h.buildIPSetHashDataForConfig(config),
		ListMD5s:   h.calculateListHashesForConfig(config, usedLists),
		DNSServers: sortedDNS,
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
	defer utils.CloseOrWarn(file)

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
	General    *GeneralConfig    `json:"general"`
	IPSets     []*IPSetHashData  `json:"ipsets"`
	ListMD5s   map[string]string `json:"list_md5s"`
	DNSServers []string          `json:"dns_servers"` // Resolved DNS server strings
}

// IPSetHashData represents hashable IPSet configuration
type IPSetHashData struct {
	IPSetName           string          `json:"ipset_name"`
	Lists               []string        `json:"lists"`
	IPVersion           IPFamily        `json:"ip_version"`
	FlushBeforeApplying bool            `json:"flush_before_applying"`
	Routing             *RoutingConfig  `json:"routing"`
	IPTablesRules       []*IPTablesRule `json:"iptables_rules"`
}

// sortedStrings returns a sorted copy of a string slice
func sortedStrings(s []string) []string {
	result := make([]string, len(s))
	copy(result, s)
	sort.Strings(result)
	return result
}
