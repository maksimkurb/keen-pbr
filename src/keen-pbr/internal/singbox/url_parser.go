package singbox

import (
	"encoding/base64"
	"fmt"
	"net/url"
	"strconv"
	"strings"
)

// ProxyURL represents a parsed proxy URL
type ProxyURL struct {
	Scheme   string
	Username string
	Password string
	Hostname string
	Port     uint16
	Name     string
	Params   map[string]string
}

// ParseURL parses a proxy URL string into a ProxyURL struct
func ParseURL(rawURL string, defaultPort uint16) (*ProxyURL, error) {
	u, err := url.Parse(rawURL)
	if err != nil {
		return nil, fmt.Errorf("failed to parse URL: %w", err)
	}

	proxyURL := &ProxyURL{
		Scheme: strings.ToLower(u.Scheme),
		Params: make(map[string]string),
	}

	// Extract hostname and port
	proxyURL.Hostname = u.Hostname()
	if u.Port() != "" {
		port, err := strconv.ParseUint(u.Port(), 10, 16)
		if err != nil {
			return nil, fmt.Errorf("invalid port: %w", err)
		}
		proxyURL.Port = uint16(port)
	} else {
		proxyURL.Port = defaultPort
	}

	// Extract username and password
	if u.User != nil {
		proxyURL.Username = u.User.Username()
		proxyURL.Password, _ = u.User.Password()
	}

	// Extract fragment as name
	proxyURL.Name = u.Fragment
	if proxyURL.Name == "" {
		proxyURL.Name = proxyURL.Hostname
	}

	// Parse query parameters
	query := u.Query()
	for key, values := range query {
		// Normalize key (lowercase, remove underscores)
		normalizedKey := strings.ToLower(strings.ReplaceAll(key, "_", ""))
		// Join multiple values with comma
		proxyURL.Params[normalizedKey] = strings.Join(values, ",")
	}

	return proxyURL, nil
}

// GetParam returns a parameter value, checking multiple possible keys
func (u *ProxyURL) GetParam(keys ...string) string {
	for _, key := range keys {
		normalizedKey := strings.ToLower(strings.ReplaceAll(key, "_", ""))
		if val, ok := u.Params[normalizedKey]; ok {
			return val
		}
	}
	return ""
}

// GetParamInt returns a parameter as integer with default value
func (u *ProxyURL) GetParamInt(defaultVal int, keys ...string) int {
	val := u.GetParam(keys...)
	if val == "" {
		return defaultVal
	}
	if i, err := strconv.Atoi(val); err == nil {
		return i
	}
	return defaultVal
}

// GetParamBool returns a parameter as boolean
func (u *ProxyURL) GetParamBool(keys ...string) bool {
	val := u.GetParam(keys...)
	return val == "1" || val == "true" || val == "yes"
}

// DecodeBase64 decodes a base64 string with fault tolerance
func DecodeBase64(s string) string {
	// Try standard encoding
	if decoded, err := base64.StdEncoding.DecodeString(s); err == nil {
		return string(decoded)
	}
	// Try URL encoding
	if decoded, err := base64.URLEncoding.DecodeString(s); err == nil {
		return string(decoded)
	}
	// Try raw URL encoding
	if decoded, err := base64.RawURLEncoding.DecodeString(s); err == nil {
		return string(decoded)
	}
	// Try raw standard encoding
	if decoded, err := base64.RawStdEncoding.DecodeString(s); err == nil {
		return string(decoded)
	}
	// Return original if all fail
	return s
}
