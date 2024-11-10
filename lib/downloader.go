package lib

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
)

func DownloadLists(config *Config) error {
	client := &http.Client{}

	listsDir := filepath.Clean(config.General.ListsOutputDir)
	if err := os.MkdirAll(listsDir, 0755); err != nil {
		return fmt.Errorf("failed to create lists directory: %v", err)
	}

	for _, list := range config.List {
		log.Printf("Downloading list \"%s\" from URL: %s", list.Name, list.URL)

		resp, err := client.Get(list.URL)
		if err != nil {
			log.Printf("Failed to download list \"%s\": %v", list.Name, err)
			continue
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			log.Printf("Failed to download list \"%s\": %s", list.Name, resp.Status)
			continue
		}

		content, err := io.ReadAll(resp.Body)
		if err != nil {
			log.Printf("Failed to read response for list \"%s\": %v", list.Name, err)
			continue
		}

		path := filepath.Join(listsDir, fmt.Sprintf("%s.lst", list.Name))
		if err := os.WriteFile(path, content, 0644); err != nil {
			return fmt.Errorf("failed to write list file: %v", err)
		}
	}

	return nil
}
