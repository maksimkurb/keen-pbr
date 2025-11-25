package lists

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/hashing"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// DownloadList downloads a single list from its URL.
// Returns (changed, error) where changed indicates if the file was updated.
// Returns error if the list has no URL or download fails.
func DownloadList(list *config.ListSource, cfg *config.Config) (bool, error) {
	if list.URL == "" {
		return false, fmt.Errorf("list \"%s\" has no URL configured", list.ListName)
	}

	listsDir := filepath.Clean(cfg.General.ListsOutputDir)
	if err := os.MkdirAll(listsDir, 0755); err != nil {
		return false, fmt.Errorf("failed to create lists directory: %v", err)
	}

	log.Infof("Downloading list \"%s\" from URL: %s", list.ListName, list.URL)

	client := &http.Client{}
	resp, err := client.Get(list.URL)
	if err != nil {
		return false, fmt.Errorf("failed to download list \"%s\": %v", list.ListName, err)
	}
	defer utils.CloseOrWarn(resp.Body)
	bodyProxy := hashing.NewMD5ReaderProxy(resp.Body)

	if resp.StatusCode != http.StatusOK {
		return false, fmt.Errorf("failed to download list \"%s\": %s", list.ListName, resp.Status)
	}

	content, err := io.ReadAll(bodyProxy)
	if err != nil {
		return false, fmt.Errorf("failed to read response for list \"%s\": %v", list.ListName, err)
	}

	filePath, err := list.GetAbsolutePath(cfg)
	if err != nil {
		return false, err
	}

	if changed, err := IsFileChanged(bodyProxy, filePath); err != nil {
		log.Errorf("Failed to calculate list \"%s\" checksum: %v", list.ListName, err)
	} else if !changed {
		log.Infof("List \"%s\" is not changed, skipping write to disk", list.ListName)
		return false, nil
	}

	if err := os.WriteFile(filePath, content, 0644); err != nil {
		return false, fmt.Errorf("failed to write list file to %s: %v", filePath, err)
	}
	if err := WriteChecksum(bodyProxy, filePath); err != nil {
		return false, fmt.Errorf("failed to write list checksum: %v", err)
	}

	log.Infof("List \"%s\" downloaded successfully", list.ListName)
	return true, nil
}

// DownloadListsIfMissing downloads all lists that have URLs configured.
// Only downloads lists whose files don't exist yet (missing lists).
func DownloadListsIfMissing(config *config.Config) error {
	for _, list := range config.Lists {
		if list.URL == "" {
			continue
		}

		// Check if file already exists
		if filePath, err := list.GetAbsolutePath(config); err == nil {
			if _, err := os.Stat(filePath); err == nil {
				log.Debugf("List \"%s\" file already exists at %s, skipping download", list.ListName, filePath)
				continue
			} else {
				log.Infof("List \"%s\" file not found at %s, downloading...", list.ListName, filePath)
			}
		}

		if _, err := DownloadList(list, config); err != nil {
			log.Errorf("Error downloading list \"%s\": %v", list.ListName, err)
			continue
		}
	}

	return nil
}

// DownloadListsIfUpdated downloads all lists with URLs and updates them if needed
func DownloadListsIfUpdated(config *config.Config) error {
	for _, list := range config.Lists {
		if list.URL == "" {
			continue
		}

		if _, err := DownloadList(list, config); err != nil {
			log.Errorf("Error downloading list \"%s\": %v", list.ListName, err)
			continue
		}
	}

	return nil
}
