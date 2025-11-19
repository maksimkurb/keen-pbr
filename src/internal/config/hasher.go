package config

import (
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"sort"
)

// ConfigHasher calculates MD5 hash of configuration state
type ConfigHasher struct {
	config *Config
}

// NewConfigHasher creates a new config hasher
func NewConfigHasher(cfg *Config) *ConfigHasher {
	return &ConfigHasher{config: cfg}
}

// CalculateHash generates MD5 hash of entire configuration
func (h *ConfigHasher) CalculateHash() (string, error) {
	// 1. Get used list names from all ipsets
	usedLists := h.getUsedListNames()

	// 2. Build hashable structure
	hashData := &ConfigHashData{
		General:  h.config.General,
		IPSets:   h.buildIPSetHashData(),
		ListMD5s: h.calculateListHashes(usedLists),
	}

	// 3. Serialize to JSON (sorted keys for determinism)
	jsonBytes, err := json.Marshal(hashData)
	if err != nil {
		return "", fmt.Errorf("failed to marshal config data: %w", err)
	}

	// 4. Calculate MD5
	hash := md5.Sum(jsonBytes)
	return hex.EncodeToString(hash[:]), nil
}

// getUsedListNames returns set of list names actually used in ipsets
func (h *ConfigHasher) getUsedListNames() map[string]bool {
	used := make(map[string]bool)
	for _, ipset := range h.config.IPSets {
		for _, listName := range ipset.Lists {
			used[listName] = true
		}
	}
	return used
}

// buildIPSetHashData creates hashable representation of ipsets
func (h *ConfigHasher) buildIPSetHashData() []*IPSetHashData {
	result := make([]*IPSetHashData, len(h.config.IPSets))
	for i, ipset := range h.config.IPSets {
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

// calculateListHashes generates MD5 for each used list
func (h *ConfigHasher) calculateListHashes(usedLists map[string]bool) map[string]string {
	hashes := make(map[string]string)

	for _, list := range h.config.Lists {
		if !usedLists[list.ListName] {
			continue // Skip unused lists
		}

		hash, err := h.calculateListHash(list)
		if err != nil {
			// Use error as hash to indicate problem
			hashes[list.ListName] = fmt.Sprintf("error:%v", err)
		} else {
			hashes[list.ListName] = hash
		}
	}

	return hashes
}

// calculateListHash calculates hash for a single list
func (h *ConfigHasher) calculateListHash(list *ListSource) (string, error) {
	switch list.Type() {
	case "url":
		return h.hashListFile(list)
	case "file":
		return h.hashListFile(list)
	case "hosts":
		return h.hashInlineHosts(list)
	default:
		return "", fmt.Errorf("unknown list type")
	}
}

// hashListFile calculates MD5 of a file-based list
func (h *ConfigHasher) hashListFile(list *ListSource) (string, error) {
	path, err := list.GetAbsolutePath(h.config)
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
	General  *GeneralConfig            `json:"general"`
	IPSets   []*IPSetHashData          `json:"ipsets"`
	ListMD5s map[string]string         `json:"list_md5s"`
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

// sortedStrings returns a sorted copy of a string slice
func sortedStrings(s []string) []string {
	result := make([]string, len(s))
	copy(result, s)
	sort.Strings(result)
	return result
}
