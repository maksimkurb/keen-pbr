package api

import (
	"encoding/json"
	"fmt"
	"net/http"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

// ListResponse represents a list in API responses
type ListResponse struct {
	ListName string   `json:"list_name"`
	Type     string   `json:"type"`
	URL      string   `json:"url,omitempty"`
	File     string   `json:"file,omitempty"`
	Hosts    []string `json:"hosts,omitempty"`
}

// ListRequest represents a list in API requests
type ListRequest struct {
	ListName string   `json:"list_name"`
	URL      string   `json:"url,omitempty"`
	File     string   `json:"file,omitempty"`
	Hosts    []string `json:"hosts,omitempty"`
}

// toResponse converts config.ListSource to ListResponse
func listToResponse(list *config.ListSource) ListResponse {
	return ListResponse{
		ListName: list.ListName,
		Type:     list.Type(),
		URL:      list.URL,
		File:     list.File,
		Hosts:    list.Hosts,
	}
}

// HandleListsList returns all lists
func HandleListsList(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		lists := make([]ListResponse, 0, len(cfg.Lists))
		for _, list := range cfg.Lists {
			lists = append(lists, listToResponse(list))
		}

		RespondOK(w, lists)
	}
}

// HandleListsGet returns a single list
func HandleListsGet(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		listName := chi.URLParam(r, "list_name")

		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		// Find list
		for _, list := range cfg.Lists {
			if list.ListName == listName {
				RespondOK(w, listToResponse(list))
				return
			}
		}

		RespondNotFound(w, fmt.Sprintf("List '%s' not found", listName))
	}
}

// HandleListsCreate creates a new list
func HandleListsCreate(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req ListRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		// Validation
		if req.ListName == "" {
			RespondValidationError(w, "list_name is required")
			return
		}

		// Exactly one source must be specified
		sourceCount := 0
		if req.URL != "" {
			sourceCount++
		}
		if req.File != "" {
			sourceCount++
		}
		if len(req.Hosts) > 0 {
			sourceCount++
		}

		if sourceCount == 0 {
			RespondValidationError(w, "One of 'url', 'file', or 'hosts' must be specified")
			return
		}
		if sourceCount > 1 {
			RespondValidationError(w, "Only one of 'url', 'file', or 'hosts' can be specified")
			return
		}

		// Modify config
		err := ModifyConfig(configPath, func(cfg *config.Config) error {
			// Check if list already exists
			for _, list := range cfg.Lists {
				if list.ListName == req.ListName {
					return fmt.Errorf("list '%s' already exists", req.ListName)
				}
			}

			// Create new list
			newList := &config.ListSource{
				ListName: req.ListName,
				URL:      req.URL,
				File:     req.File,
				Hosts:    req.Hosts,
			}

			cfg.Lists = append(cfg.Lists, newList)
			return nil
		})

		if err != nil {
			if err.Error() == fmt.Sprintf("list '%s' already exists", req.ListName) {
				RespondConflict(w, err.Error())
			} else {
				RespondInternalError(w, err.Error())
			}
			return
		}

		// Load updated config to get the created list
		cfg, _ := LoadConfig(configPath)
		for _, list := range cfg.Lists {
			if list.ListName == req.ListName {
				RespondCreated(w, listToResponse(list))
				return
			}
		}

		RespondInternalError(w, "Failed to retrieve created list")
	}
}

// HandleListsUpdate updates an existing list
func HandleListsUpdate(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		listName := chi.URLParam(r, "list_name")

		var req ListRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		// Exactly one source must be specified
		sourceCount := 0
		if req.URL != "" {
			sourceCount++
		}
		if req.File != "" {
			sourceCount++
		}
		if len(req.Hosts) > 0 {
			sourceCount++
		}

		if sourceCount == 0 {
			RespondValidationError(w, "One of 'url', 'file', or 'hosts' must be specified")
			return
		}
		if sourceCount > 1 {
			RespondValidationError(w, "Only one of 'url', 'file', or 'hosts' can be specified")
			return
		}

		// Modify config
		err := ModifyConfig(configPath, func(cfg *config.Config) error {
			// Find and update list
			for _, list := range cfg.Lists {
				if list.ListName == listName {
					list.URL = req.URL
					list.File = req.File
					list.Hosts = req.Hosts
					return nil
				}
			}
			return fmt.Errorf("list '%s' not found", listName)
		})

		if err != nil {
			if err.Error() == fmt.Sprintf("list '%s' not found", listName) {
				RespondNotFound(w, err.Error())
			} else {
				RespondInternalError(w, err.Error())
			}
			return
		}

		// Load updated config to get the updated list
		cfg, _ := LoadConfig(configPath)
		for _, list := range cfg.Lists {
			if list.ListName == listName {
				RespondOK(w, listToResponse(list))
				return
			}
		}

		RespondInternalError(w, "Failed to retrieve updated list")
	}
}

// HandleListsDelete deletes a list
func HandleListsDelete(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		listName := chi.URLParam(r, "list_name")

		// Modify config
		err := ModifyConfig(configPath, func(cfg *config.Config) error {
			// Check if list is referenced by any ipsets
			referencingIPSets := []string{}
			for _, ipset := range cfg.IPSets {
				for _, list := range ipset.Lists {
					if list == listName {
						referencingIPSets = append(referencingIPSets, ipset.IPSetName)
						break
					}
				}
			}

			if len(referencingIPSets) > 0 {
				return fmt.Errorf("list '%s' is referenced by ipsets: %v", listName, referencingIPSets)
			}

			// Find and remove list
			for i, list := range cfg.Lists {
				if list.ListName == listName {
					cfg.Lists = append(cfg.Lists[:i], cfg.Lists[i+1:]...)
					return nil
				}
			}

			return fmt.Errorf("list '%s' not found", listName)
		})

		if err != nil {
			errMsg := err.Error()
			if errMsg == fmt.Sprintf("list '%s' not found", listName) {
				RespondNotFound(w, errMsg)
			} else if len(errMsg) > 0 && errMsg[:4] == "list" && contains(errMsg, "is referenced by ipsets") {
				RespondConflict(w, errMsg)
			} else {
				RespondInternalError(w, errMsg)
			}
			return
		}

		RespondNoContent(w)
	}
}

// contains checks if string contains substring
func contains(s, substr string) bool {
	return len(s) >= len(substr) && (s == substr || len(s) > len(substr) && (s[:len(substr)] == substr || s[len(s)-len(substr):] == substr || containsMiddle(s, substr)))
}

func containsMiddle(s, substr string) bool {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}
