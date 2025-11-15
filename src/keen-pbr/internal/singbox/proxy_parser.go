package singbox

import (
	"fmt"
	"net/url"
	"strings"
)

// ParseProxyURL parses a proxy URL and returns a sing-box outbound configuration
func ParseProxyURL(tag, proxyURL string) (map[string]interface{}, error) {
	// Parse URL to determine protocol
	u, err := url.Parse(proxyURL)
	if err != nil {
		return nil, fmt.Errorf("invalid proxy URL: %w", err)
	}

	scheme := strings.ToLower(u.Scheme)

	// Route to appropriate parser based on scheme
	var outbound map[string]interface{}
	switch scheme {
	case "vless":
		outbound, err = ParseVLESS(tag, proxyURL)
	case "vmess":
		outbound, err = ParseVMess(tag, proxyURL)
	case "ss", "shadowsocks":
		outbound, err = ParseShadowsocks(tag, proxyURL)
	case "ssr":
		outbound, err = ParseShadowsocksR(tag, proxyURL)
	case "trojan":
		outbound, err = ParseTrojan(tag, proxyURL)
	case "socks", "socks4", "socks4a", "socks5", "socks5h":
		outbound, err = ParseSOCKS(tag, proxyURL)
	case "ssh":
		outbound, err = ParseSSH(tag, proxyURL)
	case "wg", "wireguard":
		outbound, err = ParseWireGuard(tag, proxyURL)
	case "http", "https":
		// HTTP proxy
		outbound, err = parseHTTP(tag, proxyURL)
	case "hysteria":
		return nil, fmt.Errorf("Hysteria protocol is not yet implemented")
	case "hysteria2", "hy2":
		return nil, fmt.Errorf("Hysteria2 protocol is not yet implemented")
	case "tuic":
		return nil, fmt.Errorf("TUIC protocol is not yet implemented")
	default:
		return nil, fmt.Errorf("unsupported proxy protocol: %s", scheme)
	}

	if err != nil {
		return nil, err
	}

	return outbound, nil
}

// parseHTTP parses an HTTP/HTTPS proxy URL
func parseHTTP(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseURL(rawURL, 8080)
	if err != nil {
		return nil, fmt.Errorf("failed to parse HTTP URL: %w", err)
	}

	outbound := map[string]interface{}{
		"type":        "http",
		"tag":         tag,
		"server":      u.Hostname,
		"server_port": u.Port,
	}

	// Username and password (optional)
	if u.Username != "" {
		outbound["username"] = u.Username
	}
	if u.Password != "" {
		outbound["password"] = u.Password
	}

	// TLS for HTTPS
	if strings.ToLower(u.Scheme) == "https" {
		tls := map[string]interface{}{
			"enabled": true,
		}

		if sni := u.GetParam("sni"); sni != "" {
			tls["server_name"] = sni
		}

		if u.GetParamBool("insecure", "allowInsecure") {
			tls["insecure"] = true
		}

		outbound["tls"] = tls
	}

	return outbound, nil
}
