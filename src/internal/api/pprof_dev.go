//go:build dev

package api

import (
	"net/http/pprof"

	"github.com/go-chi/chi/v5"
)

func registerPprof(r chi.Router) {
	r.Route("/debug/pprof", func(r chi.Router) {
		r.HandleFunc("/", pprof.Index)
		r.HandleFunc("/cmdline", pprof.Cmdline)
		r.HandleFunc("/profile", pprof.Profile)
		r.HandleFunc("/symbol", pprof.Symbol)
		r.HandleFunc("/trace", pprof.Trace)
		r.Handle("/heap", pprof.Handler("heap"))
		r.Handle("/goroutine", pprof.Handler("goroutine"))
		r.Handle("/threadcreate", pprof.Handler("threadcreate"))
		r.Handle("/block", pprof.Handler("block"))
		r.Handle("/allocs", pprof.Handler("allocs"))
		r.Handle("/mutex", pprof.Handler("mutex"))
	})
}
