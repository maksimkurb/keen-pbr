package matcher

import (
	"bufio"
	"io"
	"os"
	"slices"
	"strings"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// Matcher matches domain names against configured lists and returns
// the names of ipsets that should receive the resolved IPs.
// All domains use suffix matching: "xxx.somedomain.com" matches both
// "xxx.somedomain.com" and "sub.xxx.somedomain.com", but NOT "somedomain.com".
type Matcher struct {
	mu sync.RWMutex

	// domainSuffixes maps domain suffix to list of ipset indices for suffix matching
	// e.g., "xxx.somedomain.com" in this map will match both "xxx.somedomain.com" and "sub.xxx.somedomain.com"
	domainSuffixes map[string][]uint16

	// ipsetIndexToName maps ipset index back to name
	ipsetIndexToName []string
}

// NewMatcher creates a new domain matcher from the application config.
func NewMatcher(cfg *config.Config) *Matcher {
	m := &Matcher{
		domainSuffixes:   make(map[string][]uint16),
		ipsetIndexToName: make([]string, 0),
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
	m.domainSuffixes = make(map[string][]uint16)
	m.ipsetIndexToName = make([]string, 0, len(cfg.IPSets))

	// Build ipset index mapping
	for _, ipset := range cfg.IPSets {
		m.ipsetIndexToName = append(m.ipsetIndexToName, ipset.IPSetName)
	}

	// Build list name -> list source mapping
	listsByName := make(map[string]*config.ListSource)
	for _, list := range cfg.Lists {
		listsByName[list.ListName] = list
	}

	// Process each ipset
	for ipsetIdx, ipset := range cfg.IPSets {
		// ipsetIdx now comes from the range index

		// Process each list referenced by this ipset
		for _, listName := range ipset.Lists {
			list, exists := listsByName[listName]
			if !exists {
				log.Warnf("List %s referenced by ipset %s not found", listName, ipset.IPSetName)
				continue
			}

			m.processListForIPSet(list, cfg, uint16(ipsetIdx))
		}
	}

	log.Debugf("Matcher rebuilt: %d domain suffixes",
		len(m.domainSuffixes))
}

// processListForIPSet processes a list and adds its domains to the matcher.
func (m *Matcher) processListForIPSet(list *config.ListSource, cfg *config.Config, ipsetIdx uint16) {
	// Process inline hosts
	if list.Hosts != nil {
		for _, host := range list.Hosts {
			m.addDomain(host, ipsetIdx)
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
		m.processListFile(path, ipsetIdx)
	}
}

// processListFile reads a list file and adds domains to the matcher.
func (m *Matcher) processListFile(path string, ipsetIdx uint16) {
	// We need to read the file and process each line
	// Using the same iteration pattern as lists package

	file, err := openFile(path)
	if err != nil {
		log.Debugf("Could not open list file %s: %v", path, err)
		return
	}
	defer utils.CloseOrWarn(file)

	scanner := newScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Only process DNS names, skip IPs and CIDRs
		if utils.IsDNSName(line) {
			m.addDomain(line, ipsetIdx)
		}
	}
}

// addDomain adds a domain to the matcher for the given ipset.
// Domains are matched using suffix matching: adding "xxx.somedomain.com" will match
// both "xxx.somedomain.com" and "sub.xxx.somedomain.com", but NOT "somedomain.com".
func (m *Matcher) addDomain(domain string, ipsetIdx uint16) {
	domain = strings.TrimSpace(domain)
	if domain == "" {
		return
	}

	// Normalize domain to lowercase
	domain = strings.ToLower(domain)

	// Strip wildcard prefix if present (*.example.com -> example.com)
	// Both "*.example.com" and "example.com" should behave the same way
	domain = strings.TrimPrefix(domain, "*.")

	// Add to domain suffixes for suffix matching
	// This allows "xxx.somedomain.com" to match both "xxx.somedomain.com" and "sub.xxx.somedomain.com"
	m.domainSuffixes[domain] = appendUniqueInt(m.domainSuffixes[domain], ipsetIdx)
}

// Match returns the list of ipset names that match the given domain.
// Only the most specific match is returned. For example, if both "example.com"
// and "sub.example.com" are configured, querying "sub.sub.example.com" will
// only return ipsets associated with "sub.example.com" (the most specific match).
func (m *Matcher) Match(domain string) []string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	domain = strings.ToLower(domain)

	// Find the most specific match by checking all configured domains
	var bestMatch []uint16
	var bestSpecificity uint8

	for configuredDomain, indices := range m.domainSuffixes {
		matches, specificity := utils.MatchDomain(domain, configuredDomain)
		if matches && specificity > bestSpecificity {
			bestSpecificity = specificity
			bestMatch = indices
		}
	}

	if bestMatch != nil {
		return indicesToNames(m.ipsetIndexToName, bestMatch)
	}

	// No match found - return nil to avoid allocation
	return nil
}

// indicesToNames converts a slice of indices to ipset names.
func indicesToNames(indexToName []string, indices []uint16) []string {
	result := make([]string, 0, len(indices))
	for _, idx := range indices {
		result = append(result, indexToName[idx])
	}
	return result
}

// Stats returns matcher statistics.
func (m *Matcher) Stats() (exactCount, wildcardCount int) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	// Return the same value for both since we now use a single map
	return len(m.domainSuffixes), len(m.domainSuffixes)
}

// appendUniqueInt appends an int to a slice if it's not already present.
func appendUniqueInt(slice []uint16, n uint16) []uint16 {
	if slices.Contains(slice, n) {
		return slice
	}
	return append(slice, n)
}

func openFile(path string) (*os.File, error) {
	return os.Open(path)
}

func newScanner(r io.Reader) *bufio.Scanner {
	return bufio.NewScanner(r)
}
