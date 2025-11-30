package utils

import "testing"

func TestMatchDomain(t *testing.T) {
	tests := []struct {
		name            string
		sourceDomain    string
		matchesDomain   string
		wantMatches     bool
		wantSpecificity uint8
	}{
		{
			name:            "exact match - single level",
			sourceDomain:    "com",
			matchesDomain:   "com",
			wantMatches:     true,
			wantSpecificity: 1,
		},
		{
			name:            "exact match - two levels",
			sourceDomain:    "domain.com",
			matchesDomain:   "domain.com",
			wantMatches:     true,
			wantSpecificity: 2,
		},
		{
			name:            "exact match - four levels",
			sourceDomain:    "some.sub.domain.com",
			matchesDomain:   "some.sub.domain.com",
			wantMatches:     true,
			wantSpecificity: 4,
		},
		{
			name:            "suffix match - TLD",
			sourceDomain:    "some.sub.domain.com",
			matchesDomain:   "com",
			wantMatches:     true,
			wantSpecificity: 1,
		},
		{
			name:            "suffix match - two levels",
			sourceDomain:    "some.sub.domain.com",
			matchesDomain:   "domain.com",
			wantMatches:     true,
			wantSpecificity: 2,
		},
		{
			name:            "suffix match - three levels",
			sourceDomain:    "some.sub.domain.com",
			matchesDomain:   "sub.domain.com",
			wantMatches:     true,
			wantSpecificity: 3,
		},
		{
			name:            "no match - different domain",
			sourceDomain:    "example.com",
			matchesDomain:   "other.com",
			wantMatches:     false,
			wantSpecificity: 0,
		},
		{
			name:            "no match - not a suffix",
			sourceDomain:    "subdomain.com",
			matchesDomain:   "domain.com",
			wantMatches:     false,
			wantSpecificity: 0,
		},
		{
			name:            "no match - different domain root",
			sourceDomain:    "domain.com",
			matchesDomain:   "somedomain.com",
			wantMatches:     false,
			wantSpecificity: 0,
		},
		{
			name:            "no match - source shorter than match",
			sourceDomain:    "com",
			matchesDomain:   "domain.com",
			wantMatches:     false,
			wantSpecificity: 0,
		},
		{
			name:            "case insensitive - uppercase source",
			sourceDomain:    "SOME.SUB.DOMAIN.COM",
			matchesDomain:   "domain.com",
			wantMatches:     true,
			wantSpecificity: 2,
		},
		{
			name:            "case insensitive - uppercase match",
			sourceDomain:    "some.sub.domain.com",
			matchesDomain:   "DOMAIN.COM",
			wantMatches:     true,
			wantSpecificity: 2,
		},
		{
			name:            "case insensitive - mixed case",
			sourceDomain:    "SoMe.SuB.DoMaIn.CoM",
			matchesDomain:   "sUb.DoMaIn.cOm",
			wantMatches:     true,
			wantSpecificity: 3,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotMatches, gotSpecificity := MatchDomain(tt.sourceDomain, tt.matchesDomain)
			if gotMatches != tt.wantMatches {
				t.Errorf("MatchDomain() gotMatches = %v, want %v", gotMatches, tt.wantMatches)
			}
			if gotSpecificity != tt.wantSpecificity {
				t.Errorf("MatchDomain() gotSpecificity = %v, want %v", gotSpecificity, tt.wantSpecificity)
			}
		})
	}
}
