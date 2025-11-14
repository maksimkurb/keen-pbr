package models

import (
	"encoding/json"
	"fmt"
	"time"
)

// ListType represents the type of list
type ListType string

const (
	ListTypeInline ListType = "inline"
	ListTypeLocal  ListType = "local"
	ListTypeRemote ListType = "remote"
)

// ListFormat represents the format of the list file
type ListFormat string

const (
	ListFormatSource ListFormat = "source"
	ListFormatBinary ListFormat = "binary"
)

// List is an interface for different list types
type List interface {
	GetType() ListType
	Validate() error
	GetEntryCount() int
}

// InlineList represents a list with inline entries
type InlineList struct {
	Type    ListType `json:"type"`
	Entries []string `json:"entries"`
}

func (l *InlineList) GetType() ListType {
	return ListTypeInline
}

func (l *InlineList) Validate() error {
	// Inline lists can be empty
	return nil
}

func (l *InlineList) GetEntryCount() int {
	return 0
}

// LocalList represents a list stored locally
type LocalList struct {
	Type   ListType   `json:"type"`
	Path   string     `json:"path"`
	Format ListFormat `json:"format"`
}

func (l *LocalList) GetType() ListType {
	return ListTypeLocal
}

func (l *LocalList) Validate() error {
	if l.Path == "" {
		return fmt.Errorf("path is required for local list")
	}
	if l.Format != ListFormatSource && l.Format != ListFormatBinary {
		return fmt.Errorf("format must be 'source' or 'binary'")
	}
	return nil
}

func (l *LocalList) GetEntryCount() int {
	// TODO: Implement counting entries from file
	return 0
}

// RemoteList represents a list fetched from a remote URL
type RemoteList struct {
	Type           ListType      `json:"type"`
	URL            string        `json:"url"`
	UpdateInterval time.Duration `json:"updateInterval"`
	Format         ListFormat    `json:"format"`
	LastUpdate     *time.Time    `json:"lastUpdate,omitempty"`
}

func (r *RemoteList) GetType() ListType {
	return ListTypeRemote
}

func (r *RemoteList) Validate() error {
	if r.URL == "" {
		return fmt.Errorf("url is required for remote list")
	}
	if r.UpdateInterval <= 0 {
		return fmt.Errorf("updateInterval must be positive")
	}
	if r.Format != ListFormatSource && r.Format != ListFormatBinary {
		return fmt.Errorf("format must be 'source' or 'binary'")
	}
	return nil
}

func (r *RemoteList) GetEntryCount() int {
	// TODO: Implement counting entries from cached remote list
	return 0
}

// UnmarshalJSON implements custom unmarshaling for List interface
func UnmarshalList(data []byte) (List, error) {
	var typeCheck struct {
		Type ListType `json:"type"`
	}

	if err := json.Unmarshal(data, &typeCheck); err != nil {
		return nil, err
	}

	switch typeCheck.Type {
	case ListTypeInline:
		var inline InlineList
		if err := json.Unmarshal(data, &inline); err != nil {
			return nil, err
		}
		return &inline, nil
	case ListTypeLocal:
		var local LocalList
		if err := json.Unmarshal(data, &local); err != nil {
			return nil, err
		}
		return &local, nil
	case ListTypeRemote:
		var remote RemoteList
		if err := json.Unmarshal(data, &remote); err != nil {
			return nil, err
		}
		return &remote, nil
	default:
		return nil, fmt.Errorf("unknown list type: %s", typeCheck.Type)
	}
}

// MarshalList marshals a List interface to JSON
func MarshalList(list List) ([]byte, error) {
	return json.Marshal(list)
}
