package api

import (
	"net/http"
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

// responseWriter wraps http.ResponseWriter to capture status code.
type responseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.statusCode = code
	rw.ResponseWriter.WriteHeader(code)
}
