package dnsproxy

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

func TestMatcher_ExactMatch(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"example.com", "test.org"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	// Test exact match
	matches := matcher.Match("example.com")
	if len(matches) != 1 || matches[0] != "vpn_routes" {
		t.Errorf("expected [vpn_routes], got %v", matches)
	}

	// Test no match
	matches = matcher.Match("other.com")
	if len(matches) != 0 {
		t.Errorf("expected [], got %v", matches)
	}
}

func TestMatcher_WildcardMatch(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"*.example.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	// Test subdomain match
	matches := matcher.Match("sub.example.com")
	if len(matches) != 1 || matches[0] != "vpn_routes" {
		t.Errorf("expected [vpn_routes] for sub.example.com, got %v", matches)
	}

	// Test base domain match (wildcard also matches base)
	matches = matcher.Match("example.com")
	if len(matches) != 1 || matches[0] != "vpn_routes" {
		t.Errorf("expected [vpn_routes] for example.com, got %v", matches)
	}

	// Test deep subdomain
	matches = matcher.Match("deep.sub.example.com")
	if len(matches) != 1 || matches[0] != "vpn_routes" {
		t.Errorf("expected [vpn_routes] for deep.sub.example.com, got %v", matches)
	}

	// Test non-match
	matches = matcher.Match("other.com")
	if len(matches) != 0 {
		t.Errorf("expected [] for other.com, got %v", matches)
	}
}

func TestMatcher_MultipleIPSets(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "list-a",
				Hosts:    []string{"example.com"},
			},
			{
				ListName: "list-b",
				Hosts:    []string{"example.com", "test.org"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "ipset_a",
				Lists:     []string{"list-a"},
				IPVersion: config.Ipv4,
			},
			{
				IPSetName: "ipset_b",
				Lists:     []string{"list-b"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	// example.com should match both ipsets
	matches := matcher.Match("example.com")
	if len(matches) != 2 {
		t.Errorf("expected 2 matches for example.com, got %v", matches)
	}

	// test.org should match only ipset_b
	matches = matcher.Match("test.org")
	if len(matches) != 1 || matches[0] != "ipset_b" {
		t.Errorf("expected [ipset_b] for test.org, got %v", matches)
	}
}

func TestMatcher_CaseInsensitive(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"Example.COM"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	// Test case-insensitive match
	matches := matcher.Match("example.com")
	if len(matches) != 1 {
		t.Errorf("expected 1 match, got %v", matches)
	}

	matches = matcher.Match("EXAMPLE.COM")
	if len(matches) != 1 {
		t.Errorf("expected 1 match for uppercase, got %v", matches)
	}
}

func TestMatcher_Rebuild(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"example.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	// Initial match
	matches := matcher.Match("example.com")
	if len(matches) != 1 {
		t.Errorf("expected 1 match initially, got %v", matches)
	}

	// Rebuild with new config
	newCfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"other.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher.Rebuild(newCfg)

	// example.com should no longer match
	matches = matcher.Match("example.com")
	if len(matches) != 0 {
		t.Errorf("expected 0 matches after rebuild, got %v", matches)
	}

	// other.com should now match
	matches = matcher.Match("other.com")
	if len(matches) != 1 {
		t.Errorf("expected 1 match for other.com, got %v", matches)
	}
}

func TestMatcher_SuffixMatching(t *testing.T) {
	// Test the new suffix matching behavior:
	// Adding "xxx.somedomain.com" should match:
	// - "xxx.somedomain.com" (exact)
	// - "sub.xxx.somedomain.com" (subdomain)
	// But NOT:
	// - "somedomain.com" (parent domain)
	// - "123xxx.somedomain.com" (different subdomain at same level)

	cfg := &config.Config{
		Lists: []*config.ListSource{
			{
				ListName: "test-list",
				Hosts:    []string{"xxx.somedomain.com"},
			},
		},
		IPSets: []*config.IPSetConfig{
			{
				IPSetName: "vpn_routes",
				Lists:     []string{"test-list"},
				IPVersion: config.Ipv4,
			},
		},
	}

	matcher := NewMatcher(cfg)

	tests := []struct {
		domain      string
		shouldMatch bool
		description string
	}{
		{"xxx.somedomain.com", true, "exact match"},
		{"sub.xxx.somedomain.com", true, "subdomain should match"},
		{"deep.sub.xxx.somedomain.com", true, "deep subdomain should match"},
		{"somedomain.com", false, "parent domain should NOT match"},
		{"123xxx.somedomain.com", false, "different subdomain should NOT match"},
		{"other.com", false, "unrelated domain should NOT match"},
		{"xxxsomedomain.com", false, "similar but different domain should NOT match"},
	}

	for _, tt := range tests {
		matches := matcher.Match(tt.domain)
		matched := len(matches) > 0

		if matched != tt.shouldMatch {
			if tt.shouldMatch {
				t.Errorf("%s: expected domain '%s' to match, but got no matches", tt.description, tt.domain)
			} else {
				t.Errorf("%s: expected domain '%s' NOT to match, but got matches: %v", tt.description, tt.domain, matches)
			}
		}
	}
}
