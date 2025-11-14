package api

import (
	"embed"
	"io/fs"
	"net/http"
)

//go:embed dist/*
var staticFiles embed.FS

// ServeStatic returns an http.Handler that serves embedded static files
func ServeStatic() http.Handler {
	// Strip the "dist" prefix from the embedded files
	fsys, err := fs.Sub(staticFiles, "dist")
	if err != nil {
		panic(err)
	}

	return http.FileServer(http.FS(fsys))
}
