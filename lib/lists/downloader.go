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

	for _, ipset := range config.IPSets {
		for _, list := range ipset.Lists {
			if list.URL == "" {
				continue
			}

			log.Infof("Downloading list \"%s-%s\" from URL: %s", ipset.IPSetName, list.ListName, list.URL)

			resp, err := client.Get(list.URL)
			if err != nil {
				log.Errorf("Failed to download list \"%s-%s\": %v", ipset.IPSetName, list.ListName, err)
				continue
			}
			defer resp.Body.Close()
			bodyProxy := hashing.NewMD5ReaderProxy(resp.Body)

			if resp.StatusCode != http.StatusOK {
				log.Errorf("Failed to download list \"%s-%s\": %s", ipset.IPSetName, list.ListName, resp.Status)
				continue
			}

			content, err := io.ReadAll(bodyProxy)
			if err != nil {
				log.Errorf("Failed to read response for list \"%s-%s\": %v", ipset.IPSetName, list.ListName, err)
				continue
			}

			filePath := filepath.Join(listsDir, fmt.Sprintf("%s-%s.lst", ipset.IPSetName, list.ListName))
			if changed, err := IsFileChanged(bodyProxy, filePath); err != nil {
				log.Errorf("Failed to calculate list \"%s-%s\" checksum: %v", ipset.IPSetName, list.ListName, err)
			} else if !changed {
				log.Infof("List \"%s-%s\" is not changed, skipping write to disk", ipset.IPSetName, list.ListName)
				continue
			}

			if err := os.WriteFile(filePath, content, 0644); err != nil {
				return fmt.Errorf("failed to write list file to %s: %v", filePath, err)
			}
			if err := WriteChecksum(bodyProxy, filePath); err != nil {
				return fmt.Errorf("failed to write list checksum: %v", err)
			}
		}
	}

	return nil
}
