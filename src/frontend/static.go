// Package frontend provides static file serving for the web UI.
package frontend

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
)

// DefaultUIPath is the default path where frontend static files are installed.
const DefaultUIPath = "/opt/usr/share/keen-pbr/ui"

// safeFileSystem wraps http.Dir to prevent directory traversal attacks.
type safeFileSystem struct {
	root string
}

// Open implements http.FileSystem with path traversal protection.
func (fs safeFileSystem) Open(name string) (http.File, error) {
	// Clean the path to remove any . or .. components
	cleanPath := filepath.Clean(name)

	// Ensure the path doesn't escape the root
	if strings.HasPrefix(cleanPath, "..") || strings.Contains(cleanPath, "/../") {
		return nil, os.ErrNotExist
	}

	// Build the full path
	fullPath := filepath.Join(fs.root, cleanPath)

	// Verify the resolved path is still within the root directory
	absRoot, err := filepath.Abs(fs.root)
	if err != nil {
		return nil, err
	}

	absPath, err := filepath.Abs(fullPath)
	if err != nil {
		return nil, err
	}

	// Security check: ensure the absolute path is within the root
	if !strings.HasPrefix(absPath, absRoot+string(filepath.Separator)) && absPath != absRoot {
		return nil, os.ErrNotExist
	}

	return os.Open(fullPath)
}

// NewSafeFileSystem creates a new safe file system that prevents path traversal.
func NewSafeFileSystem(root string) http.FileSystem {
	return safeFileSystem{root: root}
}

// GetHTTPFileSystem returns an http.FileSystem for serving the frontend files
// from the specified path. It includes protection against path traversal attacks.
func GetHTTPFileSystem(uiPath string) http.FileSystem {
	return NewSafeFileSystem(uiPath)
}
