package singbox

import (
	"fmt"
)

// ParseTrojan parses a Trojan proxy URL
func ParseTrojan(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseProxyURL(rawURL, 443)
	if err != nil {
		return nil, fmt.Errorf("failed to parse Trojan URL: %w", err)
	}

	outbound := map[string]interface{}{
		"type":        "trojan",
		"tag":         tag,
		"server":      u.Hostname,
		"server_port": u.Port,
		"password":    u.Username, // Trojan uses username field as password
	}

	// TLS configuration (Trojan requires TLS)
	tls := map[string]interface{}{
		"enabled": true,
	}

	// SNI
	if sni := u.GetParam("sni"); sni != "" {
		tls["server_name"] = sni
	} else if u.GetParam("peer") != "" {
		tls["server_name"] = u.GetParam("peer")
	} else if u.GetParam("host") != "" {
		tls["server_name"] = u.GetParam("host")
	}

	// ALPN
	if alpn := u.GetParam("alpn"); alpn != "" {
		tls["alpn"] = []string{alpn}
	}

	// Fingerprint
	if fp := u.GetParam("fp", "fingerprint"); fp != "" {
		tls["utls"] = map[string]interface{}{
			"enabled":     true,
			"fingerprint": fp,
		}
	}

	// Insecure
	if u.GetParamBool("allowinsecure", "allowInsecure", "insecure", "skip-cert-verify") {
		tls["insecure"] = true
	}

	outbound["tls"] = tls

	// Transport configuration
	transportType := u.GetParam("type", "network")
	if transportType != "" && transportType != "tcp" {
		transport := map[string]interface{}{
			"type": transportType,
		}

		switch transportType {
		case "ws", "websocket":
			transport["type"] = "ws"
			if path := u.GetParam("path"); path != "" {
				transport["path"] = path
			}
			if host := u.GetParam("host"); host != "" {
				transport["headers"] = map[string]interface{}{
					"Host": host,
				}
			}

		case "grpc":
			if serviceName := u.GetParam("servicename", "serviceName"); serviceName != "" {
				transport["service_name"] = serviceName
			}

		case "http", "h2":
			transport["type"] = "http"
			if path := u.GetParam("path"); path != "" {
				transport["path"] = path
			}
			if host := u.GetParam("host"); host != "" {
				transport["host"] = []string{host}
			}

		case "quic":
			// QUIC doesn't need additional config in most cases

		case "httpupgrade":
			if path := u.GetParam("path"); path != "" {
				transport["path"] = path
			}
			if host := u.GetParam("host"); host != "" {
				transport["host"] = host
			}
		}

		outbound["transport"] = transport
	}

	return outbound, nil
}
