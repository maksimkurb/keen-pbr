package singbox

import (
	"fmt"
	"strings"
)

// ParseShadowsocks parses a Shadowsocks proxy URL
func ParseShadowsocks(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseProxyURL(rawURL, 443)
	if err != nil {
		return nil, fmt.Errorf("failed to parse Shadowsocks URL: %w", err)
	}

	// Method (encryption)
	method := u.Username
	if method == "" || method == "none" {
		method = "chacha20-ietf-poly1305"
	}

	// Password
	password := u.Password
	if password == "" {
		password = u.Username
	}

	outbound := map[string]interface{}{
		"type":        "shadowsocks",
		"tag":         tag,
		"server":      u.Hostname,
		"server_port": u.Port,
		"method":      method,
		"password":    password,
	}

	// Plugin
	if plugin := u.GetParam("plugin"); plugin != "" {
		outbound["plugin"] = plugin
	}

	// Plugin options
	if pluginOpts := u.GetParam("plugin-opts", "pluginopts"); pluginOpts != "" {
		outbound["plugin_opts"] = pluginOpts
	}

	// UDP relay
	if u.GetParamBool("udp") {
		// UDP is enabled by default in sing-box shadowsocks
	}

	return outbound, nil
}

// ParseShadowsocksR parses a ShadowsocksR proxy URL
// Note: ShadowsocksR is not supported by sing-box 1.12.12
func ParseShadowsocksR(tag, rawURL string) (map[string]interface{}, error) {
	// ShadowsocksR format: ssr://base64(server:port:protocol:method:obfs:password_base64/?params)
	if !strings.HasPrefix(rawURL, "ssr://") {
		return nil, fmt.Errorf("invalid ShadowsocksR URL scheme")
	}

	return nil, fmt.Errorf("ShadowsocksR is not supported by sing-box")
}
