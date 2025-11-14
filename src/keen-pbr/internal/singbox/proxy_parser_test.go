package singbox

import (
	"encoding/json"
	"testing"
)

func TestParseVLESS(t *testing.T) {
	tests := []struct {
		name    string
		url     string
		wantErr bool
	}{
		{
			name:    "VLESS with TLS and WebSocket",
			url:     "vless://uuid-here@example.com:443?security=tls&type=ws&path=/path&host=example.com&sni=example.com#test",
			wantErr: false,
		},
		{
			name:    "VLESS with Reality",
			url:     "vless://uuid-here@example.com:443?security=reality&pbk=publickey&sid=shortid&sni=example.com#test",
			wantErr: false,
		},
		{
			name:    "VLESS without UUID",
			url:     "vless://example.com:443",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := ParseProxyURL("test-tag", tt.url)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseProxyURL() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if err == nil {
				// Print the result as JSON for inspection
				jsonData, _ := json.MarshalIndent(result, "", "  ")
				t.Logf("Result:\n%s", string(jsonData))
			}
		})
	}
}

func TestParseTrojan(t *testing.T) {
	tests := []struct {
		name    string
		url     string
		wantErr bool
	}{
		{
			name:    "Trojan basic",
			url:     "trojan://password@example.com:443?sni=example.com#test",
			wantErr: false,
		},
		{
			name:    "Trojan with WebSocket",
			url:     "trojan://password@example.com:443?type=ws&path=/path&host=example.com#test",
			wantErr: false,
		},
		{
			name:    "Trojan without password",
			url:     "trojan://example.com:443",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := ParseProxyURL("test-tag", tt.url)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseProxyURL() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if err == nil {
				jsonData, _ := json.MarshalIndent(result, "", "  ")
				t.Logf("Result:\n%s", string(jsonData))
			}
		})
	}
}

func TestParseSOCKS5(t *testing.T) {
	tests := []struct {
		name    string
		url     string
		wantErr bool
	}{
		{
			name:    "SOCKS5 with auth",
			url:     "socks5://user:pass@example.com:1080",
			wantErr: false,
		},
		{
			name:    "SOCKS5 without auth",
			url:     "socks5://example.com:1080",
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := ParseProxyURL("test-tag", tt.url)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseProxyURL() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if err == nil {
				jsonData, _ := json.MarshalIndent(result, "", "  ")
				t.Logf("Result:\n%s", string(jsonData))
			}
		})
	}
}

func TestParseShadowsocks(t *testing.T) {
	tests := []struct {
		name    string
		url     string
		wantErr bool
	}{
		{
			name:    "Shadowsocks standard format",
			url:     "ss://aes-256-gcm:password@example.com:8388#test",
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := ParseProxyURL("test-tag", tt.url)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseProxyURL() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if err == nil {
				jsonData, _ := json.MarshalIndent(result, "", "  ")
				t.Logf("Result:\n%s", string(jsonData))
			}
		})
	}
}

func TestParseHTTP(t *testing.T) {
	tests := []struct {
		name    string
		url     string
		wantErr bool
	}{
		{
			name:    "HTTP proxy with auth",
			url:     "http://user:pass@example.com:8080",
			wantErr: false,
		},
		{
			name:    "HTTPS proxy",
			url:     "https://example.com:443",
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := ParseProxyURL("test-tag", tt.url)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseProxyURL() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if err == nil {
				jsonData, _ := json.MarshalIndent(result, "", "  ")
				t.Logf("Result:\n%s", string(jsonData))
			}
		})
	}
}
