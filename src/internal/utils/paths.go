package utils

import "path/filepath"

// GetAbsolutePath returns path if it was absolute, otherwise joins it with baseDir
func GetAbsolutePath(path, baseDir string) string {
	// Check if the path is already absolute
	if filepath.IsAbs(path) {
		return path
	}

	// Join the relative path with the config directory
	absolutePath := filepath.Join(baseDir, path)

	// Clean the resulting path
	absolutePath = filepath.Clean(absolutePath)

	return absolutePath
}
