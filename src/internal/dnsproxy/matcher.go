package dnsproxy

import (
	"bufio"
	"io"
	"os"
	"strings"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// Matcher matches domain names against configured lists and returns
// the names of ipsets that should receive the resolved IPs.
type Matcher struct {
	mu sync.RWMutex

	// domainToIPSets maps exact domain name to list of ipset names
	exactDomains map[string][]string

	// wildcardSuffixes maps domain suffix (without leading dot) to list of ipset names
	// e.g., "example.com" matches "*.example.com" and "example.com"
	wildcardSuffixes map[string][]string

	// ipsets stores ipset configurations for reference
	ipsets map[string]*config.IPSetConfig
}

// NewMatcher creates a new domain matcher from the application config.
func NewMatcher(cfg *config.Config) *Matcher {
	m := &Matcher{
		exactDomains:     make(map[string][]string),
		wildcardSuffixes: make(map[string][]string),
		ipsets:           make(map[string]*config.IPSetConfig),
	}

	m.rebuild(cfg)
	return m
}

// Rebuild rebuilds the matcher from the updated configuration.
func (m *Matcher) Rebuild(cfg *config.Config) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.rebuild(cfg)
}

// rebuild rebuilds the internal maps from config (must be called with lock held).
func (m *Matcher) rebuild(cfg *config.Config) {
	// Clear existing maps
	m.exactDomains = make(map[string][]string)
	m.wildcardSuffixes = make(map[string][]string)
	m.ipsets = make(map[string]*config.IPSetConfig)

	// Build list name -> list source mapping
	listsByName := make(map[string]*config.ListSource)
	for _, list := range cfg.Lists {
		listsByName[list.ListName] = list
	}

	// Process each ipset
	for _, ipset := range cfg.IPSets {
		m.ipsets[ipset.IPSetName] = ipset

		// Process each list referenced by this ipset
		for _, listName := range ipset.Lists {
			list, exists := listsByName[listName]
			if !exists {
				log.Warnf("List %s referenced by ipset %s not found", listName, ipset.IPSetName)
				continue
			}

			m.processListForIPSet(list, cfg, ipset.IPSetName)
		}
	}

	log.Debugf("Matcher rebuilt: %d exact domains, %d wildcard suffixes",
		len(m.exactDomains), len(m.wildcardSuffixes))
}

// processListForIPSet processes a list and adds its domains to the matcher.
func (m *Matcher) processListForIPSet(list *config.ListSource, cfg *config.Config, ipsetName string) {
	// Process inline hosts
	if list.Hosts != nil {
		for _, host := range list.Hosts {
			m.addDomain(host, ipsetName)
		}
		return
	}

	// Process file or URL list
	if list.URL != "" || list.File != "" {
		path, err := list.GetAbsolutePathAndCheckExists(cfg)
		if err != nil {
			log.Debugf("Could not read list %s: %v", list.ListName, err)
			return
		}

		// Read and parse the file
		m.processListFile(path, ipsetName)
	}
}

// processListFile reads a list file and adds domains to the matcher.
func (m *Matcher) processListFile(path string, ipsetName string) {
	// We need to read the file and process each line
	// Using the same iteration pattern as lists package

	file, err := openFile(path)
	if err != nil {
		log.Debugf("Could not open list file %s: %v", path, err)
		return
	}
	defer file.Close()

	scanner := newScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Only process DNS names, skip IPs and CIDRs
		if utils.IsDNSName(line) {
			m.addDomain(line, ipsetName)
		}
	}
}

// addDomain adds a domain to the matcher for the given ipset.
func (m *Matcher) addDomain(domain string, ipsetName string) {
	domain = strings.TrimSpace(domain)
	if domain == "" {
		return
	}

	// Normalize domain to lowercase
	domain = strings.ToLower(domain)

	// Check for wildcard prefix
	if strings.HasPrefix(domain, "*.") {
		// Wildcard domain - strip the "*." prefix
		suffix := domain[2:]
		m.wildcardSuffixes[suffix] = appendUnique(m.wildcardSuffixes[suffix], ipsetName)
		// Also add exact match for the base domain
		m.exactDomains[suffix] = appendUnique(m.exactDomains[suffix], ipsetName)
	} else {
		// Exact domain match
		m.exactDomains[domain] = appendUnique(m.exactDomains[domain], ipsetName)
	}
}

// Match returns the list of ipset names that match the given domain.
func (m *Matcher) Match(domain string) []string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	domain = strings.ToLower(domain)
	var result []string

	// Check exact match
	if ipsets, exists := m.exactDomains[domain]; exists {
		result = append(result, ipsets...)
	}

	// Check wildcard matches (suffix matching)
	// For domain "sub.example.com", check:
	// - "sub.example.com" (exact, already checked)
	// - "example.com" (wildcard *.example.com)
	// - "com" (wildcard *.com)
	parts := strings.Split(domain, ".")
	for i := 1; i < len(parts); i++ {
		suffix := strings.Join(parts[i:], ".")
		if ipsets, exists := m.wildcardSuffixes[suffix]; exists {
			for _, ipset := range ipsets {
				if !contains(result, ipset) {
					result = append(result, ipset)
				}
			}
		}
	}

	return result
}

// GetIPSet returns the ipset configuration for the given name.
func (m *Matcher) GetIPSet(name string) *config.IPSetConfig {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.ipsets[name]
}

// Stats returns matcher statistics.
func (m *Matcher) Stats() (exactCount, wildcardCount int) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return len(m.exactDomains), len(m.wildcardSuffixes)
}

// appendUnique appends a string to a slice if it's not already present.
func appendUnique(slice []string, s string) []string {
	for _, item := range slice {
		if item == s {
			return slice
		}
	}
	return append(slice, s)
}

// contains checks if a slice contains a string.
func contains(slice []string, s string) bool {
	for _, item := range slice {
		if item == s {
			return true
		}
	}
	return false
}

// File reading helpers to avoid import cycle with lists package

func openFile(path string) (*os.File, error) {
	return os.Open(path)
}

func newScanner(r io.Reader) *bufio.Scanner {
	return bufio.NewScanner(r)
}
