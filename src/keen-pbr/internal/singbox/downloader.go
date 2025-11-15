package singbox

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

const (
	// DefaultVersion is the default sing-box version to download
	DefaultVersion = "1.12.12"
	// GitHubReleaseURL is the GitHub releases URL template
	GitHubReleaseURL = "https://github.com/SagerNet/sing-box/releases/download/v%s/%s"
	// DefaultInstallPath is the default installation path for sing-box
	DefaultInstallPath = "/usr/local/bin/sing-box"
)

// DownloaderConfig holds configuration for the sing-box downloader
type DownloaderConfig struct {
	Version     string
	InstallPath string
}

// Downloader manages sing-box binary downloads
type Downloader struct {
	config DownloaderConfig
}

// NewDownloader creates a new sing-box downloader
func NewDownloader(config DownloaderConfig) *Downloader {
	if config.Version == "" {
		config.Version = DefaultVersion
	}
	if config.InstallPath == "" {
		config.InstallPath = DefaultInstallPath
	}
	return &Downloader{
		config: config,
	}
}

// getArchiveFilename determines the correct archive filename for the current OS and architecture
func (d *Downloader) getArchiveFilename() (string, error) {
	goos := runtime.GOOS
	goarch := runtime.GOARCH

	// Map Go architecture names to sing-box release names
	archMap := map[string]string{
		"amd64": "amd64",
		"386":   "386",
		"arm64": "arm64",
		"arm":   "armv7", // Default ARM to ARMv7
	}

	arch, ok := archMap[goarch]
	if !ok {
		return "", fmt.Errorf("unsupported architecture: %s", goarch)
	}

	// Determine file extension based on OS
	var ext string
	switch goos {
	case "linux", "darwin", "android":
		ext = "tar.gz"
	case "windows":
		ext = "zip"
	default:
		return "", fmt.Errorf("unsupported operating system: %s", goos)
	}

	// Build filename
	filename := fmt.Sprintf("sing-box-%s-%s-%s.%s", d.config.Version, goos, arch, ext)
	return filename, nil
}

// Download downloads and installs the sing-box binary
func (d *Downloader) Download() error {
	filename, err := d.getArchiveFilename()
	if err != nil {
		return fmt.Errorf("failed to determine archive filename: %w", err)
	}

	// Download the archive
	url := fmt.Sprintf(GitHubReleaseURL, d.config.Version, filename)
	fmt.Printf("Downloading sing-box from %s...\n", url)

	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("failed to download: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("download failed with status: %s", resp.Status)
	}

	// Create temporary file for download
	tmpFile, err := os.CreateTemp("", "sing-box-*.tmp")
	if err != nil {
		return fmt.Errorf("failed to create temp file: %w", err)
	}
	defer os.Remove(tmpFile.Name())

	// Download to temp file
	_, err = io.Copy(tmpFile, resp.Body)
	if err != nil {
		tmpFile.Close()
		return fmt.Errorf("failed to save download: %w", err)
	}
	tmpFile.Close()

	// Extract the binary
	if strings.HasSuffix(filename, ".tar.gz") {
		err = d.extractTarGz(tmpFile.Name())
	} else if strings.HasSuffix(filename, ".zip") {
		return fmt.Errorf("ZIP extraction not yet implemented")
	} else {
		return fmt.Errorf("unsupported archive format")
	}

	if err != nil {
		return fmt.Errorf("failed to extract archive: %w", err)
	}

	fmt.Printf("Successfully installed sing-box to %s\n", d.config.InstallPath)
	return nil
}

// extractTarGz extracts the sing-box binary from a tar.gz archive
func (d *Downloader) extractTarGz(archivePath string) error {
	// Open the archive
	file, err := os.Open(archivePath)
	if err != nil {
		return err
	}
	defer file.Close()

	// Create gzip reader
	gzr, err := gzip.NewReader(file)
	if err != nil {
		return err
	}
	defer gzr.Close()

	// Create tar reader
	tr := tar.NewReader(gzr)

	// Find and extract the sing-box binary
	for {
		header, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}

		// Look for the sing-box binary (usually in a subdirectory like sing-box-{version}-{os}-{arch}/sing-box)
		if strings.HasSuffix(header.Name, "/sing-box") || header.Name == "sing-box" {
			// Ensure install directory exists
			installDir := filepath.Dir(d.config.InstallPath)
			if err := os.MkdirAll(installDir, 0755); err != nil {
				return fmt.Errorf("failed to create install directory: %w", err)
			}

			// Create the destination file
			outFile, err := os.OpenFile(d.config.InstallPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755)
			if err != nil {
				return fmt.Errorf("failed to create binary file: %w", err)
			}
			defer outFile.Close()

			// Copy the binary
			_, err = io.Copy(outFile, tr)
			if err != nil {
				return fmt.Errorf("failed to write binary: %w", err)
			}

			return nil
		}
	}

	return fmt.Errorf("sing-box binary not found in archive")
}

// GetVersion returns the currently configured version
func (d *Downloader) GetVersion() string {
	return d.config.Version
}

// GetInstalledVersion checks if sing-box is installed and returns its version
func (d *Downloader) GetInstalledVersion() (string, error) {
	// Check if binary exists
	if _, err := os.Stat(d.config.InstallPath); os.IsNotExist(err) {
		return "", fmt.Errorf("sing-box not installed at %s", d.config.InstallPath)
	}

	// TODO: Execute sing-box version command to get actual version
	// For now, just return that it exists
	return "installed", nil
}
