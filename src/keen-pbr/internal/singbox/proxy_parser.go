package singbox

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/url"
	"strconv"
	"strings"
)

// ParseProxyURL parses a proxy URL and returns a sing-box outbound configuration
func ParseProxyURL(tag, proxyURL string) (map[string]interface{}, error) {
	u, err := url.Parse(proxyURL)
	if err != nil {
		return nil, fmt.Errorf("invalid proxy URL: %w", err)
	}

	scheme := strings.ToLower(u.Scheme)

	switch scheme {
	case "vless":
		return parseVLESS(tag, u)
	case "vmess":
		return parseVMess(tag, u)
	case "trojan":
		return parseTrojan(tag, u)
	case "ss", "shadowsocks":
		return parseShadowsocks(tag, u)
	case "socks5", "socks":
		return parseSOCKS5(tag, u)
	case "http", "https":
		return parseHTTP(tag, u)
	default:
		return nil, fmt.Errorf("unsupported proxy protocol: %s", scheme)
	}
}

// parseVLESS parses VLESS URL format: vless://uuid@server:port?params#name
func parseVLESS(tag string, u *url.URL) (map[string]interface{}, error) {
	uuid := u.User.Username()
	if uuid == "" {
		return nil, fmt.Errorf("VLESS: missing UUID")
	}

	host := u.Hostname()
	if host == "" {
		return nil, fmt.Errorf("VLESS: missing server")
	}

	port := u.Port()
	if port == "" {
		port = "443"
	}
	portNum, _ := strconv.Atoi(port)

	query := u.Query()

	outbound := map[string]interface{}{
		"type":        "vless",
		"tag":         tag,
		"server":      host,
		"server_port": portNum,
		"uuid":        uuid,
	}

	// Parse flow (optional, e.g., "xtls-rprx-vision")
	// Note: Do not confuse with encryption parameter
	if flow := query.Get("flow"); flow != "" {
		outbound["flow"] = flow
	}

	// Parse security/TLS
	security := query.Get("security")
	if security == "tls" || security == "reality" {
		tls := map[string]interface{}{
			"enabled": true,
		}

		if sni := query.Get("sni"); sni != "" {
			tls["server_name"] = sni
		}

		if alpn := query.Get("alpn"); alpn != "" {
			tls["alpn"] = strings.Split(alpn, ",")
		}

		if fp := query.Get("fp"); fp != "" {
			tls["utls"] = map[string]interface{}{
				"enabled":     true,
				"fingerprint": fp,
			}
		}

		if security == "reality" {
			if pbk := query.Get("pbk"); pbk != "" {
				tls["reality"] = map[string]interface{}{
					"enabled":    true,
					"public_key": pbk,
				}
				if sid := query.Get("sid"); sid != "" {
					tls["reality"].(map[string]interface{})["short_id"] = sid
				}
			}
		}

		outbound["tls"] = tls
	}

	// Parse transport (skip "tcp" as it's the default)
	transportType := query.Get("type")
	if transportType != "" && transportType != "tcp" {
		transport := map[string]interface{}{
			"type": transportType,
		}

		switch transportType {
		case "ws":
			if path := query.Get("path"); path != "" {
				transport["path"] = path
			}
			if host := query.Get("host"); host != "" {
				transport["headers"] = map[string]string{
					"Host": host,
				}
			}
		case "grpc":
			if serviceName := query.Get("serviceName"); serviceName != "" {
				transport["service_name"] = serviceName
			}
		case "http":
			if path := query.Get("path"); path != "" {
				transport["path"] = path
			}
			if host := query.Get("host"); host != "" {
				transport["host"] = []string{host}
			}
		}

		outbound["transport"] = transport
	}

	return outbound, nil
}

// parseVMess parses VMess URL format (base64 encoded JSON or vmess://...)
func parseVMess(tag string, u *url.URL) (map[string]interface{}, error) {
	// VMess URLs are typically base64 encoded JSON in the host part
	encoded := u.Host + u.Path
	if encoded == "" {
		return nil, fmt.Errorf("VMess: empty configuration")
	}

	// Decode base64
	decoded, err := base64.RawURLEncoding.DecodeString(encoded)
	if err != nil {
		decoded, err = base64.StdEncoding.DecodeString(encoded)
		if err != nil {
			return nil, fmt.Errorf("VMess: invalid base64 encoding: %w", err)
		}
	}

	// Parse JSON
	var vmessConfig struct {
		Add  string      `json:"add"`  // server
		Port interface{} `json:"port"` // port (can be string or int)
		ID   string      `json:"id"`   // uuid
		Aid  interface{} `json:"aid"`  // alter_id
		Net  string      `json:"net"`  // network type
		Type string      `json:"type"` // header type
		Host string      `json:"host"` // host
		Path string      `json:"path"` // path
		TLS  string      `json:"tls"`  // tls
		SNI  string      `json:"sni"`  // sni
		ALPN string      `json:"alpn"` // alpn
	}

	if err := json.Unmarshal(decoded, &vmessConfig); err != nil {
		return nil, fmt.Errorf("VMess: invalid JSON: %w", err)
	}

	// Convert port to int
	var portNum int
	switch v := vmessConfig.Port.(type) {
	case string:
		portNum, _ = strconv.Atoi(v)
	case float64:
		portNum = int(v)
	case int:
		portNum = v
	}

	outbound := map[string]interface{}{
		"type":        "vmess",
		"tag":         tag,
		"server":      vmessConfig.Add,
		"server_port": portNum,
		"uuid":        vmessConfig.ID,
		"security":    "auto",
	}

	// Parse alter_id
	if vmessConfig.Aid != nil {
		switch v := vmessConfig.Aid.(type) {
		case string:
			if aid, err := strconv.Atoi(v); err == nil {
				outbound["alter_id"] = aid
			}
		case float64:
			outbound["alter_id"] = int(v)
		case int:
			outbound["alter_id"] = v
		}
	}

	// Parse TLS
	if vmessConfig.TLS == "tls" {
		tls := map[string]interface{}{
			"enabled": true,
		}
		if vmessConfig.SNI != "" {
			tls["server_name"] = vmessConfig.SNI
		}
		if vmessConfig.ALPN != "" {
			tls["alpn"] = strings.Split(vmessConfig.ALPN, ",")
		}
		outbound["tls"] = tls
	}

	// Parse transport
	if vmessConfig.Net != "" && vmessConfig.Net != "tcp" {
		transport := map[string]interface{}{
			"type": vmessConfig.Net,
		}

		switch vmessConfig.Net {
		case "ws":
			if vmessConfig.Path != "" {
				transport["path"] = vmessConfig.Path
			}
			if vmessConfig.Host != "" {
				transport["headers"] = map[string]string{
					"Host": vmessConfig.Host,
				}
			}
		case "grpc":
			if vmessConfig.Path != "" {
				transport["service_name"] = vmessConfig.Path
			}
		case "http":
			if vmessConfig.Path != "" {
				transport["path"] = vmessConfig.Path
			}
			if vmessConfig.Host != "" {
				transport["host"] = []string{vmessConfig.Host}
			}
		}

		outbound["transport"] = transport
	}

	return outbound, nil
}

// parseTrojan parses Trojan URL format: trojan://password@server:port?params#name
func parseTrojan(tag string, u *url.URL) (map[string]interface{}, error) {
	password := u.User.Username()
	if password == "" {
		return nil, fmt.Errorf("Trojan: missing password")
	}

	host := u.Hostname()
	if host == "" {
		return nil, fmt.Errorf("Trojan: missing server")
	}

	port := u.Port()
	if port == "" {
		port = "443"
	}
	portNum, _ := strconv.Atoi(port)

	query := u.Query()

	outbound := map[string]interface{}{
		"type":        "trojan",
		"tag":         tag,
		"server":      host,
		"server_port": portNum,
		"password":    password,
	}

	// Trojan always uses TLS
	tls := map[string]interface{}{
		"enabled": true,
	}

	if sni := query.Get("sni"); sni != "" {
		tls["server_name"] = sni
	} else if host := query.Get("host"); host != "" {
		tls["server_name"] = host
	}

	if alpn := query.Get("alpn"); alpn != "" {
		tls["alpn"] = strings.Split(alpn, ",")
	}

	if fp := query.Get("fp"); fp != "" {
		tls["utls"] = map[string]interface{}{
			"enabled":     true,
			"fingerprint": fp,
		}
	}

	outbound["tls"] = tls

	// Parse transport
	transportType := query.Get("type")
	if transportType != "" && transportType != "tcp" {
		transport := map[string]interface{}{
			"type": transportType,
		}

		switch transportType {
		case "ws":
			if path := query.Get("path"); path != "" {
				transport["path"] = path
			}
			if host := query.Get("host"); host != "" {
				transport["headers"] = map[string]string{
					"Host": host,
				}
			}
		case "grpc":
			if serviceName := query.Get("serviceName"); serviceName != "" {
				transport["service_name"] = serviceName
			}
		}

		outbound["transport"] = transport
	}

	return outbound, nil
}

// parseShadowsocks parses Shadowsocks URL format: ss://method:password@server:port#name
func parseShadowsocks(tag string, u *url.URL) (map[string]interface{}, error) {
	// Shadowsocks URL can be encoded
	userinfo := u.User.String()
	if userinfo == "" {
		// Try to decode from host part
		decoded, err := base64.RawURLEncoding.DecodeString(u.Host)
		if err != nil {
			decoded, err = base64.StdEncoding.DecodeString(u.Host)
			if err != nil {
				return nil, fmt.Errorf("Shadowsocks: invalid encoding")
			}
		}

		// Parse method:password@server:port
		parts := strings.SplitN(string(decoded), "@", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("Shadowsocks: invalid format")
		}

		methodPass := strings.SplitN(parts[0], ":", 2)
		if len(methodPass) != 2 {
			return nil, fmt.Errorf("Shadowsocks: invalid method:password format")
		}

		serverPort := strings.SplitN(parts[1], ":", 2)
		if len(serverPort) != 2 {
			return nil, fmt.Errorf("Shadowsocks: invalid server:port format")
		}

		portNum, _ := strconv.Atoi(serverPort[1])

		return map[string]interface{}{
			"type":        "shadowsocks",
			"tag":         tag,
			"server":      serverPort[0],
			"server_port": portNum,
			"method":      methodPass[0],
			"password":    methodPass[1],
		}, nil
	}

	// Standard URL format
	methodPass := strings.SplitN(u.User.String(), ":", 2)
	if len(methodPass) != 2 {
		return nil, fmt.Errorf("Shadowsocks: missing method or password")
	}

	host := u.Hostname()
	if host == "" {
		return nil, fmt.Errorf("Shadowsocks: missing server")
	}

	port := u.Port()
	if port == "" {
		port = "8388"
	}
	portNum, _ := strconv.Atoi(port)

	return map[string]interface{}{
		"type":        "shadowsocks",
		"tag":         tag,
		"server":      host,
		"server_port": portNum,
		"method":      methodPass[0],
		"password":    methodPass[1],
	}, nil
}

// parseSOCKS5 parses SOCKS5 URL format: socks5://username:password@server:port
func parseSOCKS5(tag string, u *url.URL) (map[string]interface{}, error) {
	host := u.Hostname()
	if host == "" {
		return nil, fmt.Errorf("SOCKS5: missing server")
	}

	port := u.Port()
	if port == "" {
		port = "1080"
	}
	portNum, _ := strconv.Atoi(port)

	outbound := map[string]interface{}{
		"type":        "socks",
		"tag":         tag,
		"server":      host,
		"server_port": portNum,
		"version":     "5",
	}

	username := u.User.Username()
	password, _ := u.User.Password()

	if username != "" {
		outbound["username"] = username
		outbound["password"] = password
	}

	return outbound, nil
}

// parseHTTP parses HTTP/HTTPS proxy URL format: http://username:password@server:port
func parseHTTP(tag string, u *url.URL) (map[string]interface{}, error) {
	host := u.Hostname()
	if host == "" {
		return nil, fmt.Errorf("HTTP: missing server")
	}

	port := u.Port()
	if port == "" {
		if u.Scheme == "https" {
			port = "443"
		} else {
			port = "8080"
		}
	}
	portNum, _ := strconv.Atoi(port)

	outbound := map[string]interface{}{
		"type":        "http",
		"tag":         tag,
		"server":      host,
		"server_port": portNum,
	}

	username := u.User.Username()
	password, _ := u.User.Password()

	if username != "" {
		outbound["username"] = username
		outbound["password"] = password
	}

	// HTTPS proxy uses TLS
	if u.Scheme == "https" {
		outbound["tls"] = map[string]interface{}{
			"enabled": true,
		}
	}

	return outbound, nil
}
