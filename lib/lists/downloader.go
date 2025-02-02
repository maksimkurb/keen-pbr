package lists

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/hashing"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"io"
	"net/http"
	"os"
	"path/filepath"
)

func DownloadLists(config *config.Config) error {
	client := &http.Client{}

	listsDir := filepath.Clean(config.General.ListsOutputDir)
	if err := os.MkdirAll(listsDir, 0755); err != nil {
		return fmt.Errorf("failed to create lists directory: %v", err)
	}

	for _, list := range config.Lists {
		if list.URL == "" {
			continue
		}

		log.Infof("Downloading list \"%s\" from URL: %s", list.ListName, list.URL)

		resp, err := client.Get(list.URL)
		if err != nil {
			log.Errorf("Failed to download list \"%s\": %v", list.ListName, err)
			continue
		}
		defer resp.Body.Close()
		bodyProxy := hashing.NewMD5ReaderProxy(resp.Body)

		if resp.StatusCode != http.StatusOK {
			log.Errorf("Failed to download list \"%s\": %s", list.ListName, resp.Status)
			continue
		}

		content, err := io.ReadAll(bodyProxy)
		if err != nil {
			log.Errorf("Failed to read response for list \"%s\": %v", list.ListName, err)
			continue
		}

		filePath, err := list.GetAbsolutePath(config)
		if err != nil {
			return err
		}

		if changed, err := IsFileChanged(bodyProxy, filePath); err != nil {
			log.Errorf("Failed to calculate list \"%s\" checksum: %v", list.ListName, err)
		} else if !changed {
			log.Infof("List \"%s\" is not changed, skipping write to disk", list.ListName)
			continue
		}

		if err := os.WriteFile(filePath, content, 0644); err != nil {
			return fmt.Errorf("failed to write list file to %s: %v", filePath, err)
		}
		if err := WriteChecksum(bodyProxy, filePath); err != nil {
			return fmt.Errorf("failed to write list checksum: %v", err)
		}
	}

	return nil
}
