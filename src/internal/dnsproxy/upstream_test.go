package dnsproxy

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/mocks"
)

func TestParseUpstream(t *testing.T) {
	mockClient := mocks.NewMockKeeneticClient()

	tests := []struct {
		name           string
		upstreamURL    string
		keeneticClient domain.KeeneticClient
		wantType       string // "udp", "doh", "keenetic"
		wantErr        bool
	}{
		{
			name:        "UDP with port",
			upstreamURL: "udp://8.8.8.8:53",
			wantType:    "udp",
			wantErr:     false,
		},
		{
			name:        "UDP without port",
			upstreamURL: "udp://8.8.8.8",
			wantType:    "udp",
			wantErr:     false,
		},
		{
			name:        "DoH",
			upstreamURL: "doh://dns.google/dns-query",
			wantType:    "doh",
			wantErr:     false,
		},
		{
			name:        "HTTPS",
			upstreamURL: "https://dns.google/dns-query",
			wantType:    "doh",
			wantErr:     false,
		},
		{
			name:           "Keenetic",
			upstreamURL:    "keenetic://",
			keeneticClient: mockClient,
			wantType:       "keenetic",
			wantErr:        false,
		},
		{
			name:           "Keenetic missing client",
			upstreamURL:    "keenetic://",
			keeneticClient: nil,
			wantType:       "",
			wantErr:        true,
		},
		{
			name:        "Plain IP",
			upstreamURL: "8.8.8.8",
			wantType:    "udp",
			wantErr:     false,
		},
		{
			name:        "Plain IP:Port",
			upstreamURL: "8.8.8.8:53",
			wantType:    "udp",
			wantErr:     false,
		},
		{
			name:        "Invalid URL",
			upstreamURL: "::invalid::",
			wantType:    "",
			wantErr:     true,
		},
		{
			name:        "Unsupported scheme",
			upstreamURL: "ftp://8.8.8.8",
			wantType:    "",
			wantErr:     true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			u, err := ParseUpstream(tt.upstreamURL, tt.keeneticClient)
			if tt.wantErr {
				if err == nil {
					t.Errorf("ParseUpstream() error = nil, wantErr %v", tt.wantErr)
				}
				return
			}
			if err != nil {
				t.Errorf("ParseUpstream() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if u == nil {
				t.Errorf("ParseUpstream() returned nil upstream")
				return
			}

			switch tt.wantType {
			case "udp":
				if _, ok := u.(*UDPUpstream); !ok {
					t.Errorf("ParseUpstream() = %T, want *UDPUpstream", u)
				}
			case "doh":
				if _, ok := u.(*DoHUpstream); !ok {
					t.Errorf("ParseUpstream() = %T, want *DoHUpstream", u)
				}
			case "keenetic":
				if _, ok := u.(*KeeneticUpstream); !ok {
					t.Errorf("ParseUpstream() = %T, want *KeeneticUpstream", u)
				}
			}
		})
	}
}
