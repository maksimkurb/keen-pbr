package singbox

import (
	"fmt"
	"strings"
)

// ParseSSH parses an SSH proxy URL
func ParseSSH(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseURL(rawURL, 22)
	if err != nil {
		return nil, fmt.Errorf("failed to parse SSH URL: %w", err)
	}

	outbound := map[string]interface{}{
		"type":        "ssh",
		"tag":         tag,
		"server":      u.Hostname,
		"server_port": u.Port,
	}

	// Username
	if u.Username != "" {
		outbound["user"] = u.Username
	}

	// Password
	if u.Password != "" {
		outbound["password"] = u.Password
	}

	// Private keys
	if pkParam := u.GetParam("pk", "privatekey"); pkParam != "" {
		privateKeys := strings.Split(pkParam, ",")
		formattedKeys := make([]string, len(privateKeys))
		for i, key := range privateKeys {
			// Format the private key with PEM headers
			formattedKeys[i] = fmt.Sprintf("-----BEGIN OPENSSH PRIVATE KEY-----\n%s\n-----END OPENSSH PRIVATE KEY-----", key)
		}
		outbound["private_key"] = formattedKeys
	}

	// Host keys
	if hkParam := u.GetParam("hk", "hostkey"); hkParam != "" {
		hostKeys := strings.Split(hkParam, ",")
		outbound["host_key"] = hostKeys
	}

	// Host key algorithms
	if hka := u.GetParam("hka", "hostkeyalgorithms"); hka != "" {
		outbound["host_key_algorithms"] = []string{hka}
	}

	// Client version
	if cv := u.GetParam("cv", "clientversion"); cv != "" {
		outbound["client_version"] = cv
	}

	return outbound, nil
}
