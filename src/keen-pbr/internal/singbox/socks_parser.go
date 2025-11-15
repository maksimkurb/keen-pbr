package singbox

import (
	"fmt"
)

// ParseSOCKS parses a SOCKS proxy URL
func ParseSOCKS(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseURL(rawURL, 1080)
	if err != nil {
		return nil, fmt.Errorf("failed to parse SOCKS URL: %w", err)
	}

	outbound := map[string]interface{}{
		"type":        "socks",
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

	// SOCKS version
	version := u.GetParam("v", "ver", "version")
	if version != "" {
		outbound["version"] = version
	}

	// UDP support
	if u.GetParamBool("udp") {
		outbound["network"] = "udp"
	}

	return outbound, nil
}
