package lists

import (
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
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

	for _, ipset := range config.Ipset {
		for _, list := range ipset.List {
			if list.URL == "" {
				continue
			}

			log.Infof("Downloading list \"%s-%s\" from URL: %s", ipset.IpsetName, list.ListName, list.URL)

			resp, err := client.Get(list.URL)
			if err != nil {
				log.Errorf("Failed to download list \"%s-%s\": %v", ipset.IpsetName, list.ListName, err)
				continue
			}
			defer resp.Body.Close()

			if resp.StatusCode != http.StatusOK {
				log.Errorf("Failed to download list \"%s-%s\": %s", ipset.IpsetName, list.ListName, resp.Status)
				continue
			}

			content, err := io.ReadAll(resp.Body)
			if err != nil {
				log.Errorf("Failed to read response for list \"%s-%s\": %v", ipset.IpsetName, list.ListName, err)
				continue
			}

			path := filepath.Join(listsDir, fmt.Sprintf("%s-%s.lst", ipset.IpsetName, list.ListName))
			if err := os.WriteFile(path, content, 0644); err != nil {
				return fmt.Errorf("failed to write list file to %s: %v", path, err)
			}
		}
	}

	return nil
}
