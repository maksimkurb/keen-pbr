package singbox

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
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

	// Map Go architecture names to sing-box release names
	var arch string
	switch goarch {
	case "amd64":
		arch = "amd64"
	case "386":
		// Check for softfloat variant
		if os.Getenv("GO386") == "softfloat" {
			arch = "386-softfloat"
		} else {
			arch = "386"
		}
	case "arm64":
		arch = "arm64"
	case "arm":
		// Check GOARM environment variable for ARM version
		goarm := os.Getenv("GOARM")
		switch goarm {
		case "5":
			arch = "armv5"
		case "6":
			arch = "armv6"
		case "7", "":
			arch = "armv7" // Default to ARMv7
		default:
			arch = "armv7"
		}
	case "mips":
		// Check for softfloat variant
		if os.Getenv("GOMIPS") == "softfloat" {
			arch = "mips-softfloat"
		} else {
			arch = "mips-softfloat" // sing-box only provides softfloat for mips
		}
	case "mipsle":
		// Check for softfloat variant
		if os.Getenv("GOMIPS") == "softfloat" {
			arch = "mipsle-softfloat"
		} else {
			arch = "mipsle"
		}
	case "mips64":
		// Check for softfloat variant
		if os.Getenv("GOMIPS64") == "softfloat" {
			arch = "mips64-softfloat"
		} else {
			arch = "mips64-softfloat" // sing-box only provides softfloat for mips64
		}
	case "mips64le":
		// Check for softfloat variant
		if os.Getenv("GOMIPS64") == "softfloat" {
			arch = "mips64le-softfloat"
		} else {
			arch = "mips64le"
		}
	case "ppc64le":
		arch = "ppc64le"
	case "riscv64":
		arch = "riscv64"
	case "s390x":
		arch = "s390x"
	case "loong64":
		arch = "loong64"
	default:
		return "", fmt.Errorf("unsupported architecture: %s", goarch)
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
// Also serves as a verification that the binary works (exit code 0)
func (d *Downloader) GetInstalledVersion() (string, error) {
	// Check if binary exists
	if _, err := os.Stat(d.config.InstallPath); os.IsNotExist(err) {
		return "", fmt.Errorf("sing-box not installed at %s", d.config.InstallPath)
	}

	// Execute sing-box version command to get actual version and verify it works
	cmd := exec.Command(d.config.InstallPath, "version")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to execute sing-box version: %w (output: %s)", err, string(output))
	}

	// Parse version from output
	// Expected format: "sing-box version 1.12.12\n..."
	versionStr := string(output)
	lines := strings.Split(versionStr, "\n")
	if len(lines) > 0 && strings.Contains(lines[0], "version") {
		parts := strings.Fields(lines[0])
		if len(parts) >= 3 {
			return parts[2], nil
		}
	}

	return strings.TrimSpace(versionStr), nil
}

// GetStatus returns detailed status information about sing-box installation
type BinaryStatus struct {
	Exists            bool   `json:"exists"`
	Path              string `json:"path"`
	IsWorking         bool   `json:"isWorking"`
	InstalledVersion  string `json:"installedVersion,omitempty"`
	ConfiguredVersion string `json:"configuredVersion"`
	Error             string `json:"error,omitempty"`
}

// GetStatus returns the current status of the sing-box binary
func (d *Downloader) GetStatus() *BinaryStatus {
	status := &BinaryStatus{
		Path:              d.config.InstallPath,
		ConfiguredVersion: d.config.Version,
	}

	// Check if file exists
	if _, err := os.Stat(d.config.InstallPath); os.IsNotExist(err) {
		status.Exists = false
		status.IsWorking = false
		status.Error = "Binary not found at " + d.config.InstallPath
		return status
	}

	status.Exists = true

	// Get installed version - this also verifies the binary works
	version, err := d.GetInstalledVersion()
	if err != nil {
		status.IsWorking = false
		status.Error = err.Error()
	} else {
		status.IsWorking = true
		status.InstalledVersion = version
	}

	return status
}
