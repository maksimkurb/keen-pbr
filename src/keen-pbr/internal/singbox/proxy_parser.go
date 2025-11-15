package singbox

import (
	"encoding/json"
	"fmt"
	"net/url"
	"strings"

	"github.com/hiddify/ray2sing/ray2sing"
)

// ParseProxyURL parses a proxy URL and returns a sing-box outbound configuration
func ParseProxyURL(tag, proxyURL string) (map[string]interface{}, error) {
	// Parse URL to determine protocol
	u, err := url.Parse(proxyURL)
	if err != nil {
		return nil, fmt.Errorf("invalid proxy URL: %w", err)
	}

	scheme := strings.ToLower(u.Scheme)

	// For protocols not supported by Ray2Singbox, use specific parsers
	var outbound any
	switch scheme {
	case "socks", "socks5":
		outbound, err = ray2sing.SocksSingbox(proxyURL)
	case "ssh":
		outbound, err = ray2sing.SSHSingbox(proxyURL)
	case "wireguard", "wg":
		outbound, err = ray2sing.WiregaurdSingbox(proxyURL)
	default:
		// Use Ray2Singbox for all other protocols (vless, vmess, trojan, ss, http, https, etc.)
		// useXrayWhenPossible=false to use only sing-box compatible parsers
		var configJSON string
		configJSON, err = ray2sing.Ray2Singbox(proxyURL, false)
		if err != nil {
			return nil, fmt.Errorf("failed to parse proxy URL: %w", err)
		}

		// Parse the JSON response
		var config struct {
			Outbounds []map[string]interface{} `json:"outbounds"`
		}

		if err := json.Unmarshal([]byte(configJSON), &config); err != nil {
			return nil, fmt.Errorf("failed to unmarshal config: %w", err)
		}

		// Get the first outbound (should only be one for a single proxy URL)
		if len(config.Outbounds) == 0 {
			return nil, fmt.Errorf("no outbounds found in parsed config")
		}

		outbound = config.Outbounds[0]
	}

	if err != nil {
		return nil, fmt.Errorf("failed to parse %s proxy URL: %w", scheme, err)
	}

	// Convert outbound to map if it's not already
	var result map[string]interface{}
	switch v := outbound.(type) {
	case map[string]interface{}:
		result = v
	default:
		// Marshal and unmarshal to convert to map
		outboundBytes, err := json.Marshal(outbound)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal outbound: %w", err)
		}
		if err := json.Unmarshal(outboundBytes, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal outbound: %w", err)
		}
	}

	// Set the tag
	result["tag"] = tag
	return result, nil
}

