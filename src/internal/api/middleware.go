package api

import (
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// JSONContentType middleware enforces JSON content type for requests with body.
func JSONContentType(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Only check content-type for requests with body (POST, PUT, PATCH)
		if r.Method == http.MethodPost || r.Method == http.MethodPut || r.Method == http.MethodPatch {
			if r.ContentLength > 0 {
				ct := r.Header.Get("Content-Type")
				if ct != "application/json" && ct != "" {
					WriteInvalidRequest(w, "Content-Type must be application/json")
					return
				}
			}
		}
		next.ServeHTTP(w, r)
	})
}

// Logger middleware logs all HTTP requests.
func Logger(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()

		// Create a response writer wrapper to capture status code
		wrapped := &responseWriter{ResponseWriter: w, statusCode: http.StatusOK}

		next.ServeHTTP(wrapped, r)

		duration := time.Since(start)
		log.Infof("%s %s - %d (%v)", r.Method, r.URL.Path, wrapped.statusCode, duration)
	})
}

// Recovery middleware recovers from panics and returns a 500 error.
func Recovery(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if err := recover(); err != nil {
				log.Errorf("Panic recovered: %v", err)
				WriteInternalError(w, "Internal server error")
			}
		}()
		next.ServeHTTP(w, r)
	})
}

// CORS middleware adds CORS headers for localhost development.
func CORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")

		// Handle preflight requests
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}

		next.ServeHTTP(w, r)
	})
}

// PrivateSubnetOnly middleware restricts access to requests from private subnets only.
// This allows the server to bind to 0.0.0.0 while still restricting access.
func PrivateSubnetOnly(next http.Handler) http.Handler {
	// Define private subnet ranges
	privateIPBlocks := []*net.IPNet{
		// IPv4 private ranges
		{IP: net.IPv4(10, 0, 0, 0), Mask: net.CIDRMask(8, 32)},        // 10.0.0.0/8
		{IP: net.IPv4(172, 16, 0, 0), Mask: net.CIDRMask(12, 32)},     // 172.16.0.0/12
		{IP: net.IPv4(192, 168, 0, 0), Mask: net.CIDRMask(16, 32)},    // 192.168.0.0/16
		{IP: net.IPv4(127, 0, 0, 0), Mask: net.CIDRMask(8, 32)},       // 127.0.0.0/8 (localhost)
	}

	// Parse IPv6 private ranges
	_, ipv6ULA, _ := net.ParseCIDR("fc00::/7")         // IPv6 Unique Local Address
	_, ipv6LinkLocal, _ := net.ParseCIDR("fe80::/10")  // IPv6 Link-Local
	_, ipv6Loopback, _ := net.ParseCIDR("::1/128")     // IPv6 Loopback

	privateIPBlocks = append(privateIPBlocks, ipv6ULA, ipv6LinkLocal, ipv6Loopback)

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Get client IP from RemoteAddr or X-Forwarded-For header
		clientIP := getClientIP(r)

		ip := net.ParseIP(clientIP)
		if ip == nil {
			log.Warnf("Invalid client IP: %s", clientIP)
			WriteForbidden(w, "Access denied")
			return
		}

		// Check if IP is in any private subnet
		isPrivate := false
		for _, block := range privateIPBlocks {
			if block != nil && block.Contains(ip) {
				isPrivate = true
				break
			}
		}

		if !isPrivate {
			log.Warnf("Access denied from non-private IP: %s", clientIP)
			WriteForbidden(w, "Access denied: only private networks are allowed")
			return
		}

		next.ServeHTTP(w, r)
	})
}

// getClientIP extracts the client IP from the request.
// It checks X-Forwarded-For header first, then falls back to RemoteAddr.
func getClientIP(r *http.Request) string {
	// Check X-Forwarded-For header (in case of reverse proxy)
	forwarded := r.Header.Get("X-Forwarded-For")
	if forwarded != "" {
		// X-Forwarded-For can contain multiple IPs, take the first one
		ips := strings.Split(forwarded, ",")
		return strings.TrimSpace(ips[0])
	}

	// Check X-Real-IP header
	realIP := r.Header.Get("X-Real-IP")
	if realIP != "" {
		return realIP
	}

	// Fall back to RemoteAddr
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

// responseWriter wraps http.ResponseWriter to capture status code.
type responseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.statusCode = code
	rw.ResponseWriter.WriteHeader(code)
}
