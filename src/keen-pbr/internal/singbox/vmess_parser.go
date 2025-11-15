package singbox

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

// ParseVMess parses a VMess proxy URL
func ParseVMess(tag, rawURL string) (map[string]interface{}, error) {
	// VMess URLs are in format: vmess://base64(json)
	if !strings.HasPrefix(rawURL, "vmess://") {
		return nil, fmt.Errorf("invalid VMess URL scheme")
	}

	// Decode base64
	encoded := strings.TrimPrefix(rawURL, "vmess://")
	decoded := DecodeBase64(encoded)

	// Parse JSON
	var vmessConfig map[string]interface{}
	if err := json.Unmarshal([]byte(decoded), &vmessConfig); err != nil {
		return nil, fmt.Errorf("failed to parse VMess JSON: %w", err)
	}

	// Extract fields with type assertions
	getString := func(key string, defaultVal string) string {
		if val, ok := vmessConfig[key].(string); ok {
			return val
		}
		return defaultVal
	}

	getInt := func(key string, defaultVal int) int {
		switch v := vmessConfig[key].(type) {
		case float64:
			return int(v)
		case string:
			if i, err := strconv.Atoi(v); err == nil {
				return i
			}
		}
		return defaultVal
	}

	outbound := map[string]interface{}{
		"type":   "vmess",
		"tag":    tag,
		"server": getString("add", ""),
		"uuid":   getString("id", ""),
	}

	// Server port
	port := getInt("port", 443)
	outbound["server_port"] = port

	// Security (encryption method)
	security := getString("scy", "auto")
	if security == "" {
		security = "auto"
	}
	outbound["security"] = security

	// Alter ID
	if alterId := getInt("aid", 0); alterId != 0 {
		outbound["alter_id"] = alterId
	}

	// Packet encoding
	packetEncoding := getString("packetEncoding", "xudp")
	if packetEncoding == "" {
		packetEncoding = "xudp"
	}
	outbound["packet_encoding"] = packetEncoding

	// Transport/Network type
	network := getString("net", "tcp")
	if network != "" && network != "tcp" {
		transport := map[string]interface{}{
			"type": network,
		}

		switch network {
		case "ws", "websocket":
			transport["type"] = "ws"
			if path := getString("path", ""); path != "" {
				transport["path"] = path
			}
			if host := getString("host", ""); host != "" {
				transport["headers"] = map[string]interface{}{
					"Host": host,
				}
			}

		case "grpc":
			if serviceName := getString("path", ""); serviceName != "" {
				transport["service_name"] = serviceName
			}

		case "http", "h2":
			transport["type"] = "http"
			if path := getString("path", ""); path != "" {
				transport["path"] = path
			}
			if host := getString("host", ""); host != "" {
				transport["host"] = []string{host}
			}

		case "quic":
			// QUIC config if needed

		case "httpupgrade":
			if path := getString("path", ""); path != "" {
				transport["path"] = path
			}
			if host := getString("host", ""); host != "" {
				transport["host"] = host
			}
		}

		outbound["transport"] = transport
	}

	// TLS configuration
	tls := getString("tls", "")
	if tls == "tls" {
		tlsConfig := map[string]interface{}{
			"enabled": true,
		}

		if sni := getString("sni", ""); sni != "" {
			tlsConfig["server_name"] = sni
		} else if host := getString("host", ""); host != "" {
			tlsConfig["server_name"] = host
		}

		if alpn := getString("alpn", ""); alpn != "" {
			tlsConfig["alpn"] = []string{alpn}
		}

		if fp := getString("fp", ""); fp != "" {
			tlsConfig["utls"] = map[string]interface{}{
				"enabled":     true,
				"fingerprint": fp,
			}
		}

		// Allow insecure
		if getString("skip-cert-verify", "") == "true" || getString("allowInsecure", "") == "true" {
			tlsConfig["insecure"] = true
		}

		outbound["tls"] = tlsConfig
	}

	return outbound, nil
}
