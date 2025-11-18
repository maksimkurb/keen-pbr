package frontend

import (
	"embed"
	"io/fs"
	"net/http"
)

// DistFS contains the embedded frontend dist files.
// The frontend is built from src/frontend and embedded into the binary.
//
//go:embed all:dist
var DistFS embed.FS

// GetHTTPFileSystem returns an http.FileSystem for serving the embedded frontend.
// The "dist" prefix is stripped from paths.
func GetHTTPFileSystem() (http.FileSystem, error) {
	// Strip the "dist" prefix from the embedded FS
	distFS, err := fs.Sub(DistFS, "dist")
	if err != nil {
		return nil, err
	}

	return http.FS(distFS), nil
}
