//go:build !dev

package api

import "github.com/go-chi/chi/v5"

func registerPprof(r chi.Router) {
	// No-op when pprof is disabled in production builds
}
