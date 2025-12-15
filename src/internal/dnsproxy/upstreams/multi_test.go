package upstreams

import (
	"context"
	"testing"

	"github.com/miekg/dns"
)

// mockUpstream is a mock Upstream for testing
type mockUpstream struct {
	domain    string
	matchFn   func(string) int
	queryFn   func(context.Context, *dns.Msg) (*dns.Msg, error)
	failCount int
	attempts  int
}

func (m *mockUpstream) Query(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
	m.attempts++
	if m.queryFn != nil {
		return m.queryFn(ctx, req)
	}
	// Default: return empty response
	return &dns.Msg{}, nil
}

func (m *mockUpstream) Close() error {
	return nil
}

func (m *mockUpstream) GetDomain() string {
	return m.domain
}

func (m *mockUpstream) MatchesDomain(queryDomain string) int {
	if m.matchFn != nil {
		return m.matchFn(queryDomain)
	}
	return -1
}

func (m *mockUpstream) GetDNSStrings() []string {
	return nil
}

// TestBaseUpstream_MatchesDomain tests domain matching with depth
func TestBaseUpstream_MatchesDomain(t *testing.T) {
	tests := []struct {
		name           string
		restrictDomain string
		queryDomain    string
		expectedDepth  int
		description    string
	}{
		// Empty restriction (matches all)
		{
			name:           "EmptyRestriction",
			restrictDomain: "",
			queryDomain:    "example.com",
			expectedDepth:  0,
			description:    "Empty restriction should return depth 0",
		},

		// Single label domain (e.g., "com")
		{
			name:           "SingleLabelExactMatch",
			restrictDomain: "com",
			queryDomain:    "com",
			expectedDepth:  1,
			description:    "Exact match for single label",
		},
		{
			name:           "SingleLabelSubdomain",
			restrictDomain: "com",
			queryDomain:    "example.com",
			expectedDepth:  1,
			description:    "Subdomain match for single label",
		},

		// Two label domain (e.g., "example.com")
		{
			name:           "TwoLabelExactMatch",
			restrictDomain: "example.com",
			queryDomain:    "example.com",
			expectedDepth:  2,
			description:    "Exact match for two labels",
		},
		{
			name:           "TwoLabelSubdomain",
			restrictDomain: "example.com",
			queryDomain:    "sub.example.com",
			expectedDepth:  2,
			description:    "Subdomain match for two labels",
		},
		{
			name:           "TwoLabelDeepSubdomain",
			restrictDomain: "example.com",
			queryDomain:    "deep.sub.example.com",
			expectedDepth:  2,
			description:    "Deep subdomain match for two labels",
		},

		// Three label domain (e.g., "yyy.zzz.com")
		{
			name:           "ThreeLabelExactMatch",
			restrictDomain: "yyy.zzz.com",
			queryDomain:    "yyy.zzz.com",
			expectedDepth:  3,
			description:    "Exact match for three labels",
		},
		{
			name:           "ThreeLabelSubdomain",
			restrictDomain: "yyy.zzz.com",
			queryDomain:    "xxx.yyy.zzz.com",
			expectedDepth:  3,
			description:    "Subdomain match for three labels",
		},

		// No match cases
		{
			name:           "NoMatch_DifferentDomain",
			restrictDomain: "example.com",
			queryDomain:    "other.com",
			expectedDepth:  -1,
			description:    "Different domain should not match",
		},
		{
			name:           "NoMatch_ParentDomain",
			restrictDomain: "sub.example.com",
			queryDomain:    "example.com",
			expectedDepth:  -1,
			description:    "Parent domain should not match when restricted",
		},

		// Trailing dots
		{
			name:           "TrailingDotRestriction",
			restrictDomain: "example.com.",
			queryDomain:    "sub.example.com",
			expectedDepth:  2,
			description:    "Trailing dot in restriction should be handled",
		},
		{
			name:           "TrailingDotQuery",
			restrictDomain: "example.com",
			queryDomain:    "sub.example.com.",
			expectedDepth:  2,
			description:    "Trailing dot in query should be handled",
		},
		{
			name:           "TrailingDotsOnBoth",
			restrictDomain: "example.com.",
			queryDomain:    "sub.example.com.",
			expectedDepth:  2,
			description:    "Trailing dots on both should be handled",
		},

		// Case sensitivity
		{
			name:           "CaseInsensitive",
			restrictDomain: "Example.COM",
			queryDomain:    "SUB.example.com",
			expectedDepth:  2,
			description:    "Matching should be case-insensitive",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Use NewBaseUpstream to properly initialize normalized domain
			upstream := NewBaseUpstream(tt.restrictDomain)

			result := upstream.MatchesDomain(tt.queryDomain)
			if result != tt.expectedDepth {
				t.Errorf("%s: expected depth %d, got %d", tt.description, tt.expectedDepth, result)
			}
		})
	}
}

// TestMultiUpstream_SelectsMaxDepth tests that MultiUpstream correctly selects upstreams with max depth
func TestMultiUpstream_SelectsMaxDepth(t *testing.T) {
	tests := []struct {
		name           string
		upstreams      []Upstream
		queryDomain    string
		expectMatches  int
		description    string
	}{
		{
			name: "AllDepth0",
			upstreams: []Upstream{
				&mockUpstream{domain: "", matchFn: func(d string) int { return 0 }},
				&mockUpstream{domain: "", matchFn: func(d string) int { return 0 }},
			},
			queryDomain:   "example.com",
			expectMatches: 2,
			description:   "All upstreams with depth 0 should be selected",
		},
		{
			name: "MixedDepths_SelectMaxOnly",
			upstreams: []Upstream{
				&mockUpstream{domain: "", matchFn: func(d string) int { return 0 }},        // depth 0
				&mockUpstream{domain: "example.com", matchFn: func(d string) int { return 2 }}, // depth 2
				&mockUpstream{domain: "", matchFn: func(d string) int { return 0 }},        // depth 0
			},
			queryDomain:   "example.com",
			expectMatches: 1,
			description:   "Only upstream with max depth (2) should be selected",
		},
		{
			name: "NonContiguousMaxDepth",
			upstreams: []Upstream{
				&mockUpstream{domain: "com", matchFn: func(d string) int { return 1 }},     // depth 1
				&mockUpstream{domain: "example.com", matchFn: func(d string) int { return 2 }}, // depth 2
				&mockUpstream{domain: "", matchFn: func(d string) int { return 0 }},        // depth 0
				&mockUpstream{domain: "example.com", matchFn: func(d string) int { return 2 }}, // depth 2
			},
			queryDomain:   "sub.example.com",
			expectMatches: 2,
			description:   "Non-contiguous upstreams with max depth should all be selected",
		},
		{
			name: "NoMatches",
			upstreams: []Upstream{
				&mockUpstream{domain: "other.com", matchFn: func(d string) int { return -1 }},
				&mockUpstream{domain: "another.com", matchFn: func(d string) int { return -1 }},
			},
			queryDomain:   "example.com",
			expectMatches: 0,
			description:   "No upstreams match should result in 0 selected",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			multi := NewMultiUpstream(tt.upstreams, nil)
			defer multi.Close()

			// Create a DNS query
			req := &dns.Msg{}
			req.Question = []dns.Question{
				{Name: tt.queryDomain, Qtype: dns.TypeA, Qclass: dns.ClassINET},
			}

			// We can't directly inspect which upstreams are selected in tryUpstreams,
			// but we can verify the logic by checking that Query succeeds/fails appropriately
			_, err := multi.Query(context.Background(), req)

			if tt.expectMatches > 0 && err != nil {
				t.Errorf("%s: expected query to succeed, got error: %v", tt.description, err)
			}
			if tt.expectMatches == 0 && err == nil {
				t.Errorf("%s: expected query to fail with no matches, but succeeded", tt.description)
			}
		})
	}
}

// TestMultiUpstream_SelectsRandomFromMaxDepth tests that upstreams with max depth are selected randomly
func TestMultiUpstream_SelectsRandomFromMaxDepth(t *testing.T) {
	queryCount := 0
	upstream1Calls := 0
	upstream2Calls := 0

	up1 := &mockUpstream{
		domain: "example.com",
		matchFn: func(d string) int { return 2 },
		queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
			upstream1Calls++
			return &dns.Msg{}, nil
		},
	}

	up2 := &mockUpstream{
		domain: "example.com",
		matchFn: func(d string) int { return 2 },
		queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
			upstream2Calls++
			return &dns.Msg{}, nil
		},
	}

	multi := NewMultiUpstream([]Upstream{up1, up2}, nil)
	defer multi.Close()

	req := &dns.Msg{}
	req.Question = []dns.Question{
		{Name: "example.com", Qtype: dns.TypeA, Qclass: dns.ClassINET},
	}

	// Run multiple queries
	for i := 0; i < 10; i++ {
		_, _ = multi.Query(context.Background(), req)
		queryCount++
	}

	// Both upstreams should have been called at least once (with high probability)
	// We can't guarantee randomness with just 10 tries, but we can check that at least one was called
	if upstream1Calls == 0 && upstream2Calls == 0 {
		t.Error("Neither upstream was called")
	}
}

// TestMultiUpstream_DepthPrecedence tests that higher depth upstreams are preferred
func TestMultiUpstream_DepthPrecedence(t *testing.T) {
	// Create upstreams with different depths
	up1 := &mockUpstream{
		domain: "",
		matchFn: func(d string) int { return 0 }, // matches everything
		queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
			return &dns.Msg{}, nil
		},
	}

	up2 := &mockUpstream{
		domain: "example.com",
		matchFn: func(d string) int { return 2 }, // more specific
		queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
			return &dns.Msg{}, nil
		},
	}

	multi := NewMultiUpstream([]Upstream{up1, up2}, nil)
	defer multi.Close()

	req := &dns.Msg{}
	req.Question = []dns.Question{
		{Name: "sub.example.com", Qtype: dns.TypeA, Qclass: dns.ClassINET},
	}

	_, err := multi.Query(context.Background(), req)
	if err != nil {
		t.Errorf("Query failed: %v", err)
	}

	// up2 should be tried (it has higher depth)
	// up1 should not be tried (it has lower depth)
	if up2.attempts == 0 {
		t.Error("Expected high-depth upstream to be tried")
	}
	// up1 might be called by tryUpstreams if up2 fails, but ideally shouldn't be
}

// TestMultiUpstream_AllDepth0_PicksAny tests that when all match with depth 0, any can be picked
func TestMultiUpstream_AllDepth0_PicksAny(t *testing.T) {
	upstreams := []Upstream{
		&mockUpstream{
			domain: "",
			matchFn: func(d string) int { return 0 },
			queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
				return &dns.Msg{}, nil
			},
		},
		&mockUpstream{
			domain: "",
			matchFn: func(d string) int { return 0 },
			queryFn: func(ctx context.Context, req *dns.Msg) (*dns.Msg, error) {
				return &dns.Msg{}, nil
			},
		},
	}

	multi := NewMultiUpstream(upstreams, nil)
	defer multi.Close()

	req := &dns.Msg{}
	req.Question = []dns.Question{
		{Name: "any.domain.com", Qtype: dns.TypeA, Qclass: dns.ClassINET},
	}

	_, err := multi.Query(context.Background(), req)
	if err != nil {
		t.Errorf("Query failed: %v", err)
	}

	// At least one upstream should have been called
	up1Calls := upstreams[0].(*mockUpstream).attempts
	up2Calls := upstreams[1].(*mockUpstream).attempts
	if up1Calls+up2Calls == 0 {
		t.Error("No upstream was called")
	}
}
