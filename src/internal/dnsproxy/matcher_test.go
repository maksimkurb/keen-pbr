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
